/*
 * -----------------------------------
 * Daemon /  Revision History
 * -----------------------------------
 *
 */

#define _VERSION     "0.1.3"
#define VERSION_DATE "05.05.2024"

#ifdef GIT_REV
#  define VERSION _VERSION "-GIT" GIT_REV
#else
#  define VERSION _VERSION
#endif

/*
 * ------------------------------------

2024-05-05: version 0.1.3
   - change: improved info dialog

2024-05-05: version 0.1.2
   - fixed: Pool pump steering

2024-05-01: version 0.1.1
   - added:  raspberry pi porting
   - change: style adjustments

2024-03-03: version 0.1.0
   - added:  i2c bus components
   - added:  victron protocol
   - change: many improvements
   - added:  configurable widget color condition
   - added:  condition for automatic outputs
   - added:  demo code for systemd interface

2023-01-07: version 0.0.7
   - change: minor improvements

2022-10-24: version 0.0.6
   - change: minor improvements like vdr url

2022-07-19: version 0.0.5
   - change: git merge

2022-07-19: version 0.0.4
   - change: development

2022-01-31: version 0.0.3
   - change: first release of common homectld version

2021-05-25: version 0.0.2
   - change: changes and fixed

2020-04-16: version 0.0.1
   - start of implementation

 * ------------------------------------
 */
