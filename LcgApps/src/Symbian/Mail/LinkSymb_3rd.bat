set APP_ID=a000b86f
set APP_NAME=ProfiMail

set L=_build\Mail\S60_3rd_Release
set CAPS=ReadUserData+WriteUserData+NetworkServices+LocalServices
call ..\..\Tools\Symbian\9.1\Link.bat %APP_NAME% %APP_ID% S60 %CAPS% #0x57b3d91a
if errorlevel 1 goto fail1

rem// create crc file for app (use app's ID as seed)
..\..\Tools\MakeCRC.exe %L%\lcg32.bin 0x%APP_ID%

rem// append self-check at end of exe
copy %L%\lcg32.bin /B + %L%\lcg32.bin.sum /B + %L%\lcg32.bin.crc %L%\lcg32.bin >nul

rem// finalize - make .sis packaged file(s)
call :prepare_shop

goto :eof

//----------------------------
:fail1
echo Can't link
goto :eof

//----------------------------
:prepare_shop

copy ..\Email\PM.dta %L% >nul

rem Pack stuff into DTA
..\..\Tools\ZipMaker +%L%\PM.dta ^
"Lang { ^
   %LANG_DIR%\ ^
}"

call :make_shop lcg
if errorlevel 1 goto :eof

echo Done

goto :eof

//----------------------------
:make_shop

rem// make sis file
makesis Symbian\Mail\S60_3rd.pkg %L%\tmp.sis >nul
if errorlevel 1 goto fail2

echo Signing (%1)...
signsis %L%\tmp.sis _build\Mail\ProfiMail_S60_3rd_%1.sisx ..\..\Tools\Symbian\9.1\lcg.cer ..\..\Tools\Symbian\9.1\lcg.key
if errorlevel 1 goto fail3
del %L%\tmp.sis
goto :eof

//----------------------------
:fail2
makesis Symbian\Mail\S60_3rd.pkg %L%\tmp.sis
goto :eof

//----------------------------
:fail3
echo can't run signsis
goto :eof
