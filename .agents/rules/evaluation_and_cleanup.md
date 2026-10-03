---
description: Rule for periodic evaluation and cleanup
---

# Periodic Evaluation and Cleanup

1. **Full-System Evaluation:**
   - After every 2-3 iterations, a full code-base evaluation must be triggered.
   - This evaluation should review the architecture, tests, and module integration for technical debt or performance regressions.
   - Ensure tests are comprehensive and the system meets the high-level architecture rules.

2. **Cleanup of Temporary Files:**
   - Periodically audit the project directory.
   - Remove any temporary, useless, or trash files generated during development (e.g., temporary build artifacts, scratch scripts that are no longer needed, duplicate test files).
   - Ensure the repository remains clean and well-organized.

3. **Research Results Documentation:**
   - Side by side, store research results or summaries of each iteration in the `docs/iterations/` folder.
   - Include performance metrics, approaches taken, rejected ideas, and why certain decisions were made.
