# Vulkan Backend Code Review — Decisions

## VK_CHECK Placement

Decided: Local VK_CHECK per .cpp file is acceptable for a learning codebase.
Alternative considered: shared VK_CHECK in a common header. Rejected because
it adds a dependency to a single-line macro and the project values file-local
clarity over DRY for trivial macros.

## Error Handling Strategy

Decided: Functions that return bool/empty-struct use manual "!= VK_SUCCESS" checks
instead of VK_CHECK. Functions that return void and cannot recover (e.g., shutdown,
post-init resource creation) use VK_CHECK→assert.

This mimics the project's "crash early, crash loud" philosophy from AGENTS.md
while allowing graceful failure for init paths.
