Build and test with sanitizers, then analyze any errors found.

Run these steps sequentially:

1. **ASan + UBSan build and test:**
   ```bash
   just clean
   just init debug
   just test debug
   ```

2. **TSan build and test:**
   ```bash
   just init debug-tsan
   just test debug-tsan
   ```

3. **Analyze results:**
   - If all tests pass cleanly under both sanitizers, report success.
   - If sanitizer errors are detected, for each error:
     - **Severity**: critical (crash/UB) / warning (potential issue)
     - **Sanitizer**: ASan / UBSan / TSan
     - **Description**: What the sanitizer detected
     - **Stack trace**: Key frames from the error output
     - **Root cause**: Analysis of why it happens
     - **Fix**: Proposed code change

   - If errors come from MuPDF internals (not our code), note them as suppressions needed and suggest adding an appropriate suppression file.

4. **Summary**: Report total issues found per sanitizer and overall pass/fail status.
