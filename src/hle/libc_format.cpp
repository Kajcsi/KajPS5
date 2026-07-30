// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/libc_format.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kajps5::hle::detail {
namespace {

constexpr std::size_t kMaximumFormatBytes = 1024 * 1024;
constexpr std::size_t kMaximumCountWrites = 4096;
constexpr std::uint32_t kGpSaveAreaBytes = 48;
constexpr std::uint32_t kFpSaveAreaEnd = 176;

enum class PrintfLength { kNone, kChar, kShort, kLong };

struct FormatResult {
  struct CountWrite {
    std::uint64_t address = 0;
    std::size_t size = 0;
    std::size_t value = 0;
  };

  HleContextStatus status = HleContextStatus::kOk;
  std::string value;
  std::vector<CountWrite> count_writes;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == HleContextStatus::kOk;
  }
};

class PrintfArgumentSource {
 public:
  virtual ~PrintfArgumentSource() = default;
  [[nodiscard]] virtual std::uint64_t NextInteger() = 0;
  [[nodiscard]] virtual double NextFloat() = 0;
  [[nodiscard]] virtual HleContextStatus status() const noexcept = 0;
};

class RegisterArgumentSource final : public PrintfArgumentSource {
 public:
  RegisterArgumentSource(HleCallContext& context, std::size_t gp_index)
      : context_(context), gp_index_(gp_index) {}

  [[nodiscard]] std::uint64_t NextInteger() override {
    std::optional<std::uint64_t> value;
    if (gp_index_ < 6) {
      value = context_.Argument(gp_index_++);
    } else {
      value = context_.Argument(6 + stack_index_++);
    }
    if (!value) {
      status_ = HleContextStatus::kInvalidArgument;
      return 0;
    }
    return *value;
  }

  [[nodiscard]] double NextFloat() override {
    if (fp_index_ < kHleVectorArgumentRegisterCount) {
      const auto vector = context_.VectorArgument(fp_index_++);
      if (!vector) {
        status_ = HleContextStatus::kInvalidArgument;
        return 0;
      }
      double value = 0;
      std::memcpy(&value, vector->data(), sizeof(value));
      return value;
    }

    const auto bits = context_.Argument(6 + stack_index_++);
    if (!bits) {
      status_ = HleContextStatus::kInvalidArgument;
      return 0;
    }
    return std::bit_cast<double>(*bits);
  }

  [[nodiscard]] HleContextStatus status() const noexcept override {
    return status_;
  }

 private:
  HleCallContext& context_;
  std::size_t gp_index_ = 0;
  std::size_t fp_index_ = 0;
  std::size_t stack_index_ = 0;
  HleContextStatus status_ = HleContextStatus::kOk;
};

class VaListArgumentSource final : public PrintfArgumentSource {
 public:
  VaListArgumentSource(HleCallContext& context, std::uint32_t gp_offset,
                       std::uint32_t fp_offset,
                       std::uint64_t overflow_area,
                       std::uint64_t register_save_area)
      : context_(context),
        gp_offset_(gp_offset),
        fp_offset_(fp_offset),
        overflow_area_(overflow_area),
        register_save_area_(register_save_area) {}

  [[nodiscard]] std::uint64_t NextInteger() override {
    std::uint64_t address = 0;
    if (register_save_area_ != 0 &&
        gp_offset_ <= kGpSaveAreaBytes - sizeof(std::uint64_t)) {
      if (!AddOffset(register_save_area_, gp_offset_, address)) {
        return 0;
      }
      gp_offset_ += sizeof(std::uint64_t);
    } else {
      address = overflow_area_;
      if (!AddOffset(overflow_area_, sizeof(std::uint64_t),
                     overflow_area_)) {
        return 0;
      }
    }
    std::uint64_t value = 0;
    if (context_.ReadUInt64(address, value) != HleContextStatus::kOk) {
      status_ = HleContextStatus::kMemoryFault;
      return 0;
    }
    return value;
  }

  [[nodiscard]] double NextFloat() override {
    std::uint64_t address = 0;
    if (register_save_area_ != 0 &&
        fp_offset_ <= kFpSaveAreaEnd - 16) {
      if (!AddOffset(register_save_area_, fp_offset_, address)) {
        return 0;
      }
      fp_offset_ += 16;
    } else {
      address = overflow_area_;
      if (!AddOffset(overflow_area_, sizeof(std::uint64_t),
                     overflow_area_)) {
        return 0;
      }
    }
    std::uint64_t bits = 0;
    if (context_.ReadUInt64(address, bits) != HleContextStatus::kOk) {
      status_ = HleContextStatus::kMemoryFault;
      return 0;
    }
    return std::bit_cast<double>(bits);
  }

  [[nodiscard]] HleContextStatus status() const noexcept override {
    return status_;
  }

 private:
  bool AddOffset(std::uint64_t base, std::uint64_t offset,
                 std::uint64_t& result) {
    if (offset > std::numeric_limits<std::uint64_t>::max() - base) {
      status_ = HleContextStatus::kMemoryFault;
      result = 0;
      return false;
    }
    result = base + offset;
    return true;
  }

  HleCallContext& context_;
  std::uint32_t gp_offset_ = 0;
  std::uint32_t fp_offset_ = 0;
  std::uint64_t overflow_area_ = 0;
  std::uint64_t register_save_area_ = 0;
  HleContextStatus status_ = HleContextStatus::kOk;
};

bool Append(std::string& output, std::string_view value) {
  if (value.size() > kMaximumFormatBytes - output.size()) {
    return false;
  }
  output.append(value);
  return true;
}

std::string Digits(std::uint64_t value, unsigned int base, bool uppercase) {
  std::array<char, 65> buffer{};
  const auto converted =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, base);
  if (converted.ec != std::errc{}) {
    return {};
  }
  std::string result(buffer.data(), converted.ptr);
  if (uppercase) {
    for (auto& character : result) {
      if (character >= 'a' && character <= 'f') {
        character = static_cast<char>(character - 'a' + 'A');
      }
    }
  }
  return result;
}

std::optional<std::string> Pad(std::string value, std::size_t width,
                               bool left, bool zero,
                               std::size_t prefix_bytes = 0) {
  if (width > kMaximumFormatBytes || value.size() >= width) {
    return value;
  }
  const auto count = width - value.size();
  if (count > kMaximumFormatBytes - value.size()) {
    return std::nullopt;
  }
  if (left) {
    value.append(count, ' ');
  } else if (zero && prefix_bytes <= value.size()) {
    value.insert(prefix_bytes, count, '0');
  } else {
    value.insert(0, count, ' ');
  }
  return value;
}

std::optional<std::string> FormatInteger(
    std::uint64_t magnitude, bool negative, unsigned int base, bool uppercase,
    bool alternate, bool show_sign, bool space_sign, std::size_t width,
    int precision, bool left, bool zero) {
  auto digits = Digits(magnitude, base, uppercase);
  if (precision == 0 && magnitude == 0) {
    digits.clear();
  }
  if (precision > 0 &&
      static_cast<std::size_t>(precision) > digits.size()) {
    digits.insert(0, static_cast<std::size_t>(precision) - digits.size(), '0');
  }

  std::string prefix;
  if (negative) {
    prefix = "-";
  } else if (show_sign) {
    prefix = "+";
  } else if (space_sign) {
    prefix = " ";
  }
  if (alternate) {
    if (base == 16 && magnitude != 0) {
      prefix += uppercase ? "0X" : "0x";
    } else if (base == 8 && (digits.empty() || digits.front() != '0')) {
      prefix += '0';
    }
  }
  if (prefix.size() > kMaximumFormatBytes - digits.size()) {
    return std::nullopt;
  }
  auto value = prefix + digits;
  return Pad(std::move(value), width, left, zero && precision < 0,
             prefix.size());
}

FormatResult ReadPrecisionString(HleCallContext& context,
                                 std::uint64_t address,
                                 std::size_t maximum,
                                 bool precision_limited) {
  if (address == 0) {
    constexpr std::string_view kNullText = "(null)";
    return {HleContextStatus::kOk,
            std::string(kNullText.substr(
                0, std::min(maximum, kNullText.size())))};
  }
  if (maximum > kMaximumFormatBytes) {
    return {HleContextStatus::kResourceLimit, {}};
  }
  std::string result;
  result.reserve(std::min<std::size_t>(maximum, 128));
  for (std::size_t index = 0; index < maximum; ++index) {
    if (index > std::numeric_limits<std::uint64_t>::max() - address) {
      return {HleContextStatus::kMemoryFault, {}};
    }
    std::array<std::byte, 1> byte{};
    if (context.ReadMemory(address + index, byte) != HleContextStatus::kOk) {
      return {HleContextStatus::kMemoryFault, {}};
    }
    if (byte[0] == std::byte{0}) {
      return {HleContextStatus::kOk, std::move(result)};
    }
    result.push_back(
        static_cast<char>(std::to_integer<unsigned char>(byte[0])));
  }
  return {precision_limited ? HleContextStatus::kOk
                            : HleContextStatus::kUnterminatedString,
          precision_limited ? std::move(result) : std::string{}};
}

std::optional<std::string> FormatFloat(double value, char specifier,
                                       int precision, bool alternate,
                                       bool show_sign, bool space_sign,
                                       std::size_t width, bool left,
                                       bool zero) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  if (specifier == 'F' || specifier == 'E' || specifier == 'G') {
    stream << std::uppercase;
  }
  if (alternate) {
    stream << std::showpoint;
  }
  if (show_sign) {
    stream << std::showpos;
  }
  if (specifier == 'f' || specifier == 'F') {
    stream << std::fixed;
  } else if (specifier == 'e' || specifier == 'E') {
    stream << std::scientific;
  } else {
    stream << std::defaultfloat;
  }
  auto applied_precision = precision >= 0 ? precision : 6;
  if ((specifier == 'g' || specifier == 'G') && applied_precision == 0) {
    applied_precision = 1;
  }
  stream << std::setprecision(applied_precision) << value;
  auto result = stream.str();
  if (!show_sign && space_sign && !std::signbit(value)) {
    result.insert(result.begin(), ' ');
  }
  const auto prefix = !result.empty() &&
                              (result.front() == '-' || result.front() == '+' ||
                               result.front() == ' ')
                          ? 1U
                          : 0U;
  return Pad(std::move(result), width, left, zero, prefix);
}

std::size_t CountWriteSize(PrintfLength length) {
  if (length == PrintfLength::kChar) {
    return 1;
  }
  if (length == PrintfLength::kShort) {
    return sizeof(std::uint16_t);
  }
  if (length == PrintfLength::kLong) {
    return sizeof(std::uint64_t);
  }
  return sizeof(std::uint32_t);
}

FormatResult Render(HleCallContext& context, std::string_view format,
                    PrintfArgumentSource& arguments) {
  FormatResult result;
  result.value.reserve(std::min<std::size_t>(format.size() + 32,
                                             kMaximumFormatBytes));
  for (std::size_t index = 0; index < format.size();) {
    if (format[index] != '%') {
      const auto start = index;
      while (index < format.size() && format[index] != '%') {
        ++index;
      }
      if (!Append(result.value, format.substr(start, index - start))) {
        return {HleContextStatus::kResourceLimit, {}};
      }
      continue;
    }
    ++index;
    if (index == format.size()) {
      if (!Append(result.value, "%")) {
        return {HleContextStatus::kResourceLimit, {}};
      }
      break;
    }

    bool left = false;
    bool show_sign = false;
    bool space_sign = false;
    bool zero = false;
    bool alternate = false;
    bool reading_flags = true;
    while (index < format.size() && reading_flags) {
      switch (format[index]) {
        case '-': left = true; break;
        case '+': show_sign = true; break;
        case ' ': space_sign = true; break;
        case '0': zero = true; break;
        case '#': alternate = true; break;
        default: reading_flags = false; continue;
      }
      ++index;
    }

    std::size_t width = 0;
    if (index < format.size() && format[index] == '*') {
      const auto raw = static_cast<std::uint32_t>(arguments.NextInteger());
      if (arguments.status() != HleContextStatus::kOk) {
        return {arguments.status(), {}};
      }
      const auto signed_width = std::bit_cast<std::int32_t>(raw);
      if (signed_width < 0) {
        left = true;
        width = static_cast<std::size_t>(
            -static_cast<std::int64_t>(signed_width));
      } else {
        width = static_cast<std::size_t>(signed_width);
      }
      ++index;
    } else {
      while (index < format.size() && format[index] >= '0' &&
             format[index] <= '9') {
        const auto digit = static_cast<std::size_t>(format[index] - '0');
        if (width > (kMaximumFormatBytes - digit) / 10) {
          return {HleContextStatus::kResourceLimit, {}};
        }
        width = width * 10 + digit;
        ++index;
      }
    }
    if (width > kMaximumFormatBytes) {
      return {HleContextStatus::kResourceLimit, {}};
    }

    int precision = -1;
    if (index < format.size() && format[index] == '.') {
      ++index;
      if (index < format.size() && format[index] == '*') {
        precision = std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(arguments.NextInteger()));
        if (arguments.status() != HleContextStatus::kOk) {
          return {arguments.status(), {}};
        }
        if (precision < 0) {
          precision = -1;
        }
        ++index;
      } else {
        precision = 0;
        while (index < format.size() && format[index] >= '0' &&
               format[index] <= '9') {
          const auto digit = format[index] - '0';
          if (precision >
              (static_cast<int>(kMaximumFormatBytes) - digit) / 10) {
            return {HleContextStatus::kResourceLimit, {}};
          }
          precision = precision * 10 + digit;
          ++index;
        }
      }
    }
    if (precision > static_cast<int>(kMaximumFormatBytes)) {
      return {HleContextStatus::kResourceLimit, {}};
    }

    auto length = PrintfLength::kNone;
    if (index + 1 < format.size() && format[index] == 'h' &&
        format[index + 1] == 'h') {
      length = PrintfLength::kChar;
      index += 2;
    } else if (index + 1 < format.size() && format[index] == 'l' &&
               format[index + 1] == 'l') {
      length = PrintfLength::kLong;
      index += 2;
    } else if (index < format.size() && format[index] == 'h') {
      length = PrintfLength::kShort;
      ++index;
    } else if (index < format.size() &&
               (format[index] == 'l' || format[index] == 'j' ||
                format[index] == 'z' || format[index] == 't' ||
                format[index] == 'L')) {
      length = PrintfLength::kLong;
      ++index;
    }
    if (index == format.size()) {
      return {HleContextStatus::kInvalidArgument, {}};
    }
    const auto specifier = format[index++];
    std::optional<std::string> rendered;
    if (specifier == '%') {
      rendered = "%";
    } else if (specifier == 'd' || specifier == 'i') {
      const auto raw = arguments.NextInteger();
      if (arguments.status() != HleContextStatus::kOk) {
        return {arguments.status(), {}};
      }
      std::int64_t signed_value = 0;
      if (length == PrintfLength::kChar) {
        signed_value = static_cast<std::int8_t>(raw);
      } else if (length == PrintfLength::kShort) {
        signed_value = static_cast<std::int16_t>(raw);
      } else if (length == PrintfLength::kLong) {
        signed_value = std::bit_cast<std::int64_t>(raw);
      } else {
        signed_value = std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(raw));
      }
      const auto negative = signed_value < 0;
      const auto magnitude = negative
                                 ? 0U - static_cast<std::uint64_t>(signed_value)
                                 : static_cast<std::uint64_t>(signed_value);
      rendered = FormatInteger(magnitude, negative, 10, false, false,
                               show_sign, space_sign, width, precision, left,
                               zero);
    } else if (specifier == 'u' || specifier == 'x' || specifier == 'X' ||
               specifier == 'o') {
      auto value = arguments.NextInteger();
      if (arguments.status() != HleContextStatus::kOk) {
        return {arguments.status(), {}};
      }
      if (length == PrintfLength::kChar) {
        value = static_cast<std::uint8_t>(value);
      } else if (length == PrintfLength::kShort) {
        value = static_cast<std::uint16_t>(value);
      } else if (length != PrintfLength::kLong) {
        value = static_cast<std::uint32_t>(value);
      }
      const auto base = specifier == 'o' ? 8U
                         : specifier == 'u' ? 10U
                                            : 16U;
      rendered = FormatInteger(value, false, base, specifier == 'X',
                               alternate, false, false, width, precision,
                               left, zero);
    } else if (specifier == 'p') {
      const auto value = arguments.NextInteger();
      if (arguments.status() != HleContextStatus::kOk) {
        return {arguments.status(), {}};
      }
      rendered = value == 0 ? std::optional<std::string>("(nil)")
                            : std::optional<std::string>("0x" +
                                                         Digits(value, 16,
                                                                false));
      if (rendered) {
        rendered = Pad(std::move(*rendered), width, left, zero,
                       value == 0 ? 0 : 2);
      }
    } else if (specifier == 's') {
      if (length == PrintfLength::kLong) {
        return {HleContextStatus::kInvalidArgument, {}};
      }
      const auto address = arguments.NextInteger();
      if (arguments.status() != HleContextStatus::kOk) {
        return {arguments.status(), {}};
      }
      const auto text = ReadPrecisionString(
          context, address,
          precision >= 0 ? static_cast<std::size_t>(precision)
                         : kMaximumFormatBytes,
          precision >= 0);
      if (!text) {
        return text;
      }
      rendered = Pad(text.value, width, left, false);
    } else if (specifier == 'c') {
      const auto value = arguments.NextInteger();
      if (arguments.status() != HleContextStatus::kOk) {
        return {arguments.status(), {}};
      }
      if (length == PrintfLength::kLong) {
        return {HleContextStatus::kInvalidArgument, {}};
      }
      rendered = Pad(
          std::string(1, static_cast<char>(static_cast<std::uint8_t>(value))),
          width, left, false);
    } else if (specifier == 'f' || specifier == 'F' || specifier == 'e' ||
               specifier == 'E' || specifier == 'g' || specifier == 'G') {
      const auto value = arguments.NextFloat();
      if (arguments.status() != HleContextStatus::kOk) {
        return {arguments.status(), {}};
      }
      rendered = FormatFloat(value, specifier, precision, alternate,
                             show_sign, space_sign, width, left, zero);
    } else if (specifier == 'n') {
      const auto address = arguments.NextInteger();
      if (arguments.status() != HleContextStatus::kOk) {
        return {arguments.status(), {}};
      }
      const auto size = CountWriteSize(length);
      if (address == 0) {
        return {HleContextStatus::kInvalidArgument, {}};
      }
      if (!context.CanWriteMemory(address, size)) {
        return {HleContextStatus::kMemoryFault, {}};
      }
      if (result.count_writes.size() == kMaximumCountWrites) {
        return {HleContextStatus::kResourceLimit, {}};
      }
      result.count_writes.push_back(
          {address, size, result.value.size()});
      rendered = "";
    } else {
      std::string unknown = "%";
      unknown.push_back(specifier);
      rendered = std::move(unknown);
    }
    if (!rendered || !Append(result.value, *rendered)) {
      return {HleContextStatus::kResourceLimit, {}};
    }
  }
  return result;
}

HleContextStatus WriteOutput(HleCallContext& context,
                             std::uint64_t destination,
                             std::uint64_t buffer_size,
                             const FormatResult& result) {
  const auto copy_size = static_cast<std::size_t>(std::min<std::uint64_t>(
      result.value.size(), buffer_size == 0 ? 0 : buffer_size - 1));
  if (buffer_size != 0 &&
      (destination == 0 ||
       !context.CanWriteMemory(destination, copy_size + 1))) {
    return HleContextStatus::kMemoryFault;
  }

  for (const auto& count_write : result.count_writes) {
    if (!context.CanWriteMemory(count_write.address, count_write.size)) {
      return HleContextStatus::kMemoryFault;
    }
  }
  for (const auto& count_write : result.count_writes) {
    std::array<std::byte, sizeof(std::uint64_t)> bytes{};
    const auto value = static_cast<std::uint64_t>(count_write.value);
    for (std::size_t index = 0; index < count_write.size; ++index) {
      bytes[index] =
          static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
    if (context.WriteMemory(
            count_write.address,
            std::span(bytes).first(count_write.size)) !=
        HleContextStatus::kOk) {
      return HleContextStatus::kMemoryFault;
    }
  }

  context.SetReturn(result.value.size());
  if (buffer_size == 0) {
    return HleContextStatus::kOk;
  }
  if (copy_size != 0) {
    const auto bytes = std::as_bytes(
        std::span(result.value.data(), copy_size));
    if (context.WriteMemory(destination, bytes) != HleContextStatus::kOk) {
      return HleContextStatus::kMemoryFault;
    }
  }
  const std::array terminator = {std::byte{0}};
  return context.WriteMemory(destination + copy_size, terminator);
}

std::optional<VaListArgumentSource> MakeVaListSource(
    HleCallContext& context, std::uint64_t address) {
  if (address == 0) {
    return std::nullopt;
  }
  std::uint32_t gp_offset = 0;
  std::uint32_t fp_offset = 0;
  std::uint64_t overflow_area = 0;
  std::uint64_t register_save_area = 0;
  if (context.ReadUInt32(address, gp_offset) != HleContextStatus::kOk ||
      address > std::numeric_limits<std::uint64_t>::max() - 16 ||
      context.ReadUInt32(address + 4, fp_offset) != HleContextStatus::kOk ||
      context.ReadUInt64(address + 8, overflow_area) != HleContextStatus::kOk ||
      context.ReadUInt64(address + 16, register_save_area) !=
          HleContextStatus::kOk) {
    return std::nullopt;
  }
  if (gp_offset > kGpSaveAreaBytes || fp_offset > kFpSaveAreaEnd ||
      gp_offset % 8 != 0 || fp_offset % 16 != 0) {
    return std::nullopt;
  }
  return VaListArgumentSource(context, gp_offset, fp_offset, overflow_area,
                              register_save_area);
}

template <typename Source>
HleContextStatus FormatAndWrite(HleCallContext& context,
                                std::uint64_t destination,
                                std::uint64_t buffer_size,
                                std::uint64_t format_address,
                                Source& source) {
  const auto format = context.ReadNullTerminatedString(
      format_address, kMaximumHleStringBytes);
  if (!format) {
    context.SetReturn(0);
    return format.status;
  }
  const auto rendered = Render(context, format.value, source);
  if (!rendered) {
    context.SetReturn(0);
    return rendered.status;
  }
  return WriteOutput(context, destination, buffer_size, rendered);
}

}  // namespace

HleContextStatus LibcSnprintf(HleCallContext& context) {
  RegisterArgumentSource source(context, 3);
  return FormatAndWrite(context, context.Argument(0).value_or(0),
                        context.Argument(1).value_or(0),
                        context.Argument(2).value_or(0), source);
}

HleContextStatus LibcVsnprintf(HleCallContext& context) {
  auto source = MakeVaListSource(context, context.Argument(3).value_or(0));
  if (!source) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  return FormatAndWrite(context, context.Argument(0).value_or(0),
                        context.Argument(1).value_or(0),
                        context.Argument(2).value_or(0), *source);
}

HleContextStatus LibcSprintf(HleCallContext& context) {
  RegisterArgumentSource source(context, 2);
  const auto format = context.ReadNullTerminatedString(
      context.Argument(1).value_or(0), kMaximumHleStringBytes);
  if (!format) {
    context.SetReturn(0);
    return format.status;
  }
  const auto rendered = Render(context, format.value, source);
  if (!rendered) {
    context.SetReturn(0);
    return rendered.status;
  }
  return WriteOutput(context, context.Argument(0).value_or(0),
                     rendered.value.size() + 1, rendered);
}

HleContextStatus LibcVsprintf(HleCallContext& context) {
  auto source = MakeVaListSource(context, context.Argument(2).value_or(0));
  if (!source) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  const auto format = context.ReadNullTerminatedString(
      context.Argument(1).value_or(0), kMaximumHleStringBytes);
  if (!format) {
    context.SetReturn(0);
    return format.status;
  }
  const auto rendered = Render(context, format.value, *source);
  if (!rendered) {
    context.SetReturn(0);
    return rendered.status;
  }
  return WriteOutput(context, context.Argument(0).value_or(0),
                     rendered.value.size() + 1, rendered);
}

}  // namespace kajps5::hle::detail
