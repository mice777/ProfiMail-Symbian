set path=%IM_ROOT%\Tools\Symbian\9.1\gcc\bin;%IM_ROOT%\Tools\Symbian\9.1\Tools

set SDK_PATH=%IM_SYMB_S60_3RD_SDK%

set APP_ID=a000b86b
set L=%SDK_PATH%\Release\armv5\lib
set EPOCLIBS=%L%\Euser.dso %L%\libstdcpp.dso %L%\drtaeabi.dso %L%\scppnwdl.dso %L%\estlib.dso %L%\FbsCli.dso %L%\BitGdi.dso %L%\hswidgetpublisher.dso
rem 
set L=%SDK_PATH%\Release\armv5\urel
set EPOCLIBS=%EPOCLIBS% %L%\edll.lib %L%\edllstub.lib
set EPOCLIBS=%EPOCLIBS% %IM_ROOT%\Tools\Symbian\9.1\gcc\arm-none-symbianelf\lib\libsupc++.a %EPOCLIBS% %IM_ROOT%\Tools\Symbian\9.1\gcc\lib\gcc\arm-none-symbianelf\3.4.3\libgcc.a
set L=_build\S60_3rd_Release\
set OBJS=%L%\Main.o

rem// link
arm-none-symbianelf-ld -shared -Ttext 0x8000 -o %L%tmp.dll %OBJS% %EPOCLIBS% --entry E32Main 
rem todo: revise --target1-abs --no-undefined -nostdlib --default-symver -soname HsWidget{10009d8d}[%APP_ID%].dll -u _E32Main
if errorlevel 1 goto fail2

rem// process by elf2e32
set CAPS=ReadUserData+WriteUserData+NetworkServices+LocalServices
elf2e32 --uid1=0x10000079 --uid2=0x1000008d --uid3=0x%APP_ID% --sid=0x%APP_ID% --vid=0 --capability=%CAPS% --definput=Symbian\S60_3rd.def --targettype=DLL --elfinput=%L%tmp.dll --output=_build\HsWidget.dll --linkas=HsWidget{000a0000}[%APP_ID%].dll --libpath=%SDK_PATH%\Release\armv5\lib --defoutput=%L%def --dso=%L%dso
if errorlevel 1 goto fail3
del %L%\dso %L%\def %L%\tmp.dll
goto :eof

:fail2
echo Failed: ld
goto :eof

:fail3
echo Failed: elf2e32
goto :eof
