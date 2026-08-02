// Copyright (C) 2026 KajPS5 contributors
// Implementation and test reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "gpu/shader/recompiler/cfg/ShaderCFG.h"
#include "gpu/shader/recompiler/decompiler/ShaderDecoder.h"

namespace {

namespace cfg = kajps5::gpu::shader::recompiler::CFG;
namespace decoder = kajps5::gpu::shader::recompiler::Decoder;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "gpu_shader_cfg_test: " << message << '\n';
    ++failures;
  }
}

constexpr std::uint32_t EncodeScalarMove(std::uint32_t destination,
                                         std::uint32_t source) {
  return 0x80000000U | (0x7dU << 23U) |
         ((destination & 0x7fU) << 16U) | (0x03U << 8U) |
         (source & 0xffU);
}

constexpr std::uint32_t EncodeScalarAlu(std::uint32_t opcode,
                                        std::uint32_t destination,
                                        std::uint32_t source0,
                                        std::uint32_t source1) {
  return 0x80000000U | ((opcode & 0x7fU) << 23U) |
         ((destination & 0x7fU) << 16U) | ((source1 & 0xffU) << 8U) |
         (source0 & 0xffU);
}

constexpr std::uint32_t EncodeScalarCompare(std::uint32_t opcode,
                                            std::uint32_t source0,
                                            std::uint32_t source1) {
  return 0x80000000U | (0x7eU << 23U) | ((opcode & 0x7fU) << 16U) |
         ((source1 & 0xffU) << 8U) | (source0 & 0xffU);
}

constexpr std::uint32_t EncodeScalarBranch(std::uint32_t opcode,
                                           std::uint32_t immediate = 0) {
  return 0x80000000U | (0x7fU << 23U) | ((opcode & 0x7fU) << 16U) |
         (immediate & 0xffffU);
}

template <std::size_t Size>
bool Decode(const std::array<std::uint32_t, Size>& code,
            decoder::Program& program, std::string& error) {
  return decoder::DecodeProgram(std::span<const std::uint32_t>(code), program,
                                &error);
}

void TestLoopExitTailGetsPrivateMerge() {
  const std::array code = {
      EncodeScalarCompare(0x0a, 0, 129),
      EncodeScalarBranch(0x04, 11),
      EncodeScalarCompare(0x06, 1, 1),
      EncodeScalarBranch(0x05, 3),
      EncodeScalarCompare(0x06, 2, 2),
      EncodeScalarBranch(0x05, 3),
      EncodeScalarBranch(0x02, 0xfffbU),
      EncodeScalarMove(3, 129),
      EncodeScalarBranch(0x02, 2),
      EncodeScalarMove(4, 129),
      EncodeScalarBranch(0x02, 0),
      EncodeScalarAlu(0x00, 0, 0, 129),
      EncodeScalarBranch(0x02, 0xfff3U),
      0xbf810000U,
  };

  decoder::Program program;
  cfg::Graph graph;
  std::string error;
  Check(Decode(code, program, error), error.c_str());
  Check(cfg::BuildGraph(program, graph, &error), error.c_str());
  const auto original_block_count = graph.blocks.size();
  Check(cfg::Structurize(graph, &error), error.c_str());
  Check(graph.blocks.size() > original_block_count,
        "nested exits did not create a private loop merge");

  const auto* outer_header = graph.FindBlockByPc(0);
  const auto* inner_header = graph.FindBlockByPc(8);
  Check(outer_header != nullptr && inner_header != nullptr &&
            outer_header->terminator.loop_header &&
            inner_header->terminator.loop_header,
        "nested loop headers were not preserved");
  if (outer_header == nullptr || inner_header == nullptr) {
    return;
  }
  Check(inner_header->terminator.merge_block !=
            outer_header->terminator.continue_block,
        "the inner loop merge aliases the outer continue block");
  const auto* inner_merge = graph.FindBlock(inner_header->terminator.merge_block);
  Check(inner_merge != nullptr && inner_merge->inst_begin == inner_merge->inst_end &&
            inner_merge->terminator.kind == cfg::TerminatorKind::Branch &&
            inner_merge->terminator.true_block ==
                outer_header->terminator.continue_block,
        "the private inner merge does not forward to the outer continue block");
}

void TestConditionalHeaderIsNormalized() {
  const std::array code = {
      EncodeScalarCompare(0x06, 0, 0),
      EncodeScalarBranch(0x05, 2),
      EncodeScalarMove(1, 129),
      EncodeScalarBranch(0x02, 1),
      EncodeScalarMove(2, 129),
      EncodeScalarMove(3, 129),
      EncodeScalarCompare(0x06, 4, 4),
      EncodeScalarBranch(0x05, 0xfff8U),
      0xbf810000U,
  };

  decoder::Program program;
  cfg::Graph graph;
  std::string error;
  Check(Decode(code, program, error), error.c_str());
  Check(cfg::BuildGraph(program, graph, &error), error.c_str());
  const auto original_block_count = graph.blocks.size();
  Check(cfg::Structurize(graph, &error), error.c_str());
  Check(graph.blocks.size() > original_block_count,
        "a conditional loop header was not normalized");

  std::uint32_t loop_headers = 0;
  std::uint32_t selection_headers = 0;
  for (const auto& block : graph.blocks) {
    if (block.terminator.loop_header) {
      ++loop_headers;
      Check(block.inst_begin == block.inst_end &&
                block.terminator.kind == cfg::TerminatorKind::Branch,
            "the canonical loop header is not an empty branch block");
    } else if (block.terminator.kind ==
                   cfg::TerminatorKind::ConditionalBranch &&
               block.terminator.merge_block != UINT32_MAX) {
      ++selection_headers;
    }
  }
  Check(loop_headers == 1 && selection_headers == 1,
        "the guest conditional was not separated from its loop header");
}

void TestMultipleLatchesShareOneContinue() {
  const std::array code = {
      EncodeScalarCompare(0x0a, 0, 129),
      EncodeScalarBranch(0x04, 5),
      EncodeScalarCompare(0x06, 1, 1),
      EncodeScalarBranch(0x05, 0xfffcU),
      EncodeScalarMove(2, 129),
      EncodeScalarMove(3, 129),
      EncodeScalarBranch(0x02, 0xfff9U),
      0xbf810000U,
  };

  decoder::Program program;
  cfg::Graph graph;
  std::string error;
  Check(Decode(code, program, error), error.c_str());
  Check(cfg::BuildGraph(program, graph, &error), error.c_str());
  const auto original_block_count = graph.blocks.size();
  Check(graph.back_edges.size() == 2,
        "the fixture does not have two native backedges");
  Check(cfg::Structurize(graph, &error), error.c_str());
  Check(graph.blocks.size() == original_block_count + 1,
        "multiple latches did not create one continue block");
  Check(graph.back_edges.size() == 1 && graph.natural_loops.size() == 1,
        "multiple latches were not coalesced to one backedge");
  if (graph.natural_loops.empty()) {
    return;
  }
  const auto* continue_block =
      graph.FindBlock(graph.natural_loops.front().continue_block);
  Check(continue_block != nullptr &&
            continue_block->inst_begin == continue_block->inst_end &&
            continue_block->predecessors.size() == 2,
        "the canonical continue block does not join both latches");
}

void TestOverlappingExitsGetDistinctMerges() {
  const std::array code = {
      EncodeScalarCompare(0x06, 0, 0),
      EncodeScalarBranch(0x04, 2),
      EncodeScalarCompare(0x06, 1, 1),
      EncodeScalarBranch(0x04, 6),
      EncodeScalarCompare(0x06, 2, 2),
      EncodeScalarBranch(0x04, 4),
      EncodeScalarCompare(0x06, 3, 3),
      EncodeScalarBranch(0x04, 2),
      EncodeScalarMove(4, 129),
      0xbf810000U,
      EncodeScalarMove(5, 129),
      0xbf810000U,
  };

  decoder::Program program;
  cfg::Graph graph;
  std::string error;
  Check(Decode(code, program, error), error.c_str());
  Check(cfg::BuildGraph(program, graph, &error), error.c_str());
  Check(cfg::Structurize(graph, &error), error.c_str());

  std::vector<std::uint32_t> merges;
  for (const auto& block : graph.blocks) {
    if (block.terminator.kind != cfg::TerminatorKind::ConditionalBranch) {
      continue;
    }
    Check(block.terminator.merge_block != UINT32_MAX,
          "a conditional block has no merge");
    Check(std::find(merges.begin(), merges.end(),
                    block.terminator.merge_block) == merges.end(),
          "two conditional blocks still share a merge");
    merges.push_back(block.terminator.merge_block);
  }

  std::vector<bool> reachable(graph.blocks.size());
  std::vector<std::uint32_t> pending = {graph.entry_block};
  while (!pending.empty()) {
    const auto block_id = pending.back();
    pending.pop_back();
    if (reachable[block_id]) {
      continue;
    }
    reachable[block_id] = true;
    pending.insert(pending.end(), graph.blocks[block_id].successors.begin(),
                   graph.blocks[block_id].successors.end());
  }
  Check(std::all_of(reachable.begin(), reachable.end(),
                    [](bool value) { return value; }),
        "structurization left an unreachable block");
}

}  // namespace

int main() {
  TestLoopExitTailGetsPrivateMerge();
  TestConditionalHeaderIsNormalized();
  TestMultipleLatchesShareOneContinue();
  TestOverlappingExitsGetDistinctMerges();
  return failures == 0 ? 0 : 1;
}
