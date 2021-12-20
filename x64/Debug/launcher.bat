net sess>nul 2>&1||(powershell saps '%0'-Verb RunAs&exit)

set currentpath=%cd%

sc create CheatDriver type= kernel binPath= %currentpath%\CheatDriver.sys
sc start CheatDriver