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

## AI Use

AI tools may be used for research, reverse engineering, and development
assistance. Contributors must fully understand, review, and test all code they
submit and remain responsible for its correctness. Repository communication,
including pull-request descriptions, code comments, and issue comments, must
come from the human contributor rather than an autonomous AI agent.

Pull requests that include AI-assisted or AI-generated work should disclose
the scope of the AI involvement and describe the human review and testing
performed before submission. Unverified or untested generated changes may be
closed without review.

When submitting an AI-assisted PR:

- Clearly explain **what the change does**, **why it is needed**, and **what
  problem it solves**, using your own words.
- Describe **how you verified the change**, including the games, applications,
  or test cases used.
- Avoid excessive product-level logging. Use logging only when it provides
  meaningful diagnostic value.
- Comments should document design decisions or implementation details in your
  own words. Avoid generic AI-generated comments that merely restate what the
  code already does.
- Be prepared to answer review questions about the implementation. "The AI
  generated it" is not considered a sufficient explanation.
- Large AI-generated changes without a clear understanding of the
  implementation are unlikely to be accepted.
- If the implementation cannot be reasonably explained during code review,
  the pull request may be rejected regardless of whether it works.

The quality, correctness, maintainability, and long-term ownership of the
submitted code remain the responsibility of the contributor.
