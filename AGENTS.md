# AGENTS.md

Rules for AI coding agents working in this repo.

## C++

- Template code is NOT compiled until something instantiates it. A template
  defined in a .cpp with no caller is dead code hiding compile errors.
  Always add a test or call site that instantiates every template.
