# Guidelines
* The agent should commit changes atomically and incrementally whenever possible.
* The agent should use descriptive commit messages that describe not only what was changed but *why* the change was made.
* The agent should cleanup any untracked or unsubmitted changes (i.e. `git clean` and `git checkout`) after each submission.
* The agent *must* ensure that Win32 Debug and Win32 Release have built successfully before any changes can be submitted -- both *with* and *without* EnTT enabled.
* The agent *must* ensure that the test suite is passing before any changes can be submitted -- both *with* and *without* EnTT enabled.
