**What this changes**

**How it was checked**
- [ ] `ctest --test-dir build` passes
- [ ] Smoke-tested live (say how: flags, bounce comparison, screenshot)
- [ ] README, the user guide and CHANGELOG updated where the change is user-visible

**Audio-thread safety**: no allocation, locks or blocking calls were added to the audio thread.
