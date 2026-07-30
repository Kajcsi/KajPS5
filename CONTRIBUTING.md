# Contributing

Thanks for helping with KajPS5.

Open a focused issue before starting a large change. Describe the behavior you
want to add and the evidence that makes it useful.

For each code change:

1. Run `scripts/check-upstreams.ps1` once for the development session.
2. Keep the change focused.
3. Add or update a test.
4. Build the targets that you changed.
5. Run the related tests.
6. Record the exact upstream source when you adapt code or behavior.
7. Keep all copyright and license notices.

Do not upload games, firmware, keys, system software, crash dumps with private
data, or other copyrighted data that you cannot distribute.

Use a clear commit message. In the pull request, explain what changed, why it
matters, and how you tested it.

Write documentation, comments, logs, and errors in plain, direct language.
Prefer specific behavior and test evidence over broad claims. Remove filler,
repetition, and slogans.

## AI-assisted contributions

AI tools may help with research, reverse engineering, code, tests, or
documentation. You are still responsible for everything you submit. You must
understand the change well enough to explain, modify, debug, and maintain it
yourself.

If a contribution used AI assistance:

- Disclose the scope of the assistance in the pull request.
- Explain the change, its purpose, and its evidence in your own words.
- Describe the review and testing you completed yourself.
- Keep comments focused on design decisions and non-obvious implementation
  details. Do not add generic comments that only restate the code.
- Use logging only when it adds useful diagnostic evidence.
- Be ready to answer detailed questions about the implementation.

Write and verify your own pull-request descriptions, review replies, code
comments, and issue comments. Do not post unreviewed tool output as repository
communication. Maintainers may close a generated change when its contributor
cannot explain it or has not tested it.
