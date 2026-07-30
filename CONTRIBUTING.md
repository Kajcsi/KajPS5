# Contributing

Thank you for helping KajPS5.

Use a focused issue before you start a large change. State the behavior that
you want to add and the evidence that shows it is needed.

For each code change:

1. Run `scripts/check-upstreams.ps1` once for the development session.
2. Keep the change small.
3. Add or update a test.
4. Build the targets that you changed.
5. Run the related tests.
6. Record the upstream source when you adapt upstream code.
7. Keep all copyright and license notices.

Do not upload games, firmware, keys, system software, crash dumps with private
data, or other copyrighted data that you cannot distribute.

Use clear commit messages. Explain what changed, why it changed, and how you
tested it.

## AI-assisted contributions

AI tools can assist with research, reverse engineering, code, tests, and
documentation. The contributor remains responsible for every submitted line.
You must understand the change and be able to explain, modify, debug, and
maintain it without depending on the tool that produced it.

If a contribution used AI assistance:

- Disclose the scope of the assistance in the pull request.
- Explain the change, its purpose, and its evidence in your own words.
- State the human review and testing that you completed.
- Keep comments focused on design decisions and non-obvious implementation
  details. Do not add generic comments that only restate the code.
- Use logging only when it adds useful diagnostic evidence.
- Be ready to answer detailed review questions about the implementation.

Pull-request descriptions, review replies, code comments, and issue comments
must be written and verified by the human contributor. Do not post autonomous
AI output as repository communication. Maintainers can close generated changes
that are untested, unexplained, or not understood by the contributor.
