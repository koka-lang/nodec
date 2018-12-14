@echo off
cd %~dp0
set curdir=%CD%

@rem The visual studio solutions now target a specific windows SDK :-(
set winsdk=10.0.17763.0

@rem Defaults
set config=Debug
set platform=x64
set vsplatform=x64
set target=Build

@rem Help?
if /i "%1"=="help" goto help
if /i "%1"=="--help" goto help
if /i "%1"=="-help" goto help
if /i "%1"=="/help" goto help
if /i "%1"=="?" goto help
if /i "%1"=="-?" goto help
if /i "%1"=="--?" goto help
if /i "%1"=="/?" goto help

@rem Process arguments
:next-arg
if "%1"=="" goto args-done
if /i "%1"=="debug"        set config=Debug&goto arg-ok
if /i "%1"=="release"      set config=Release&goto arg-ok
if /i "%1"=="x86"          set platform=x86&set vsplatform=Win32&goto arg-ok
if /i "%1"=="win32"        set platform=x86&set vsplatform=Win32&goto arg-ok
if /i "%1"=="x64"          set platform=x64&set vsplatform=x64&goto arg-ok
:arg-ok
shift
goto next-arg
:args-done

echo Looking for Visual Studio 2017
@rem Check if VS2017 is already setup, and for the requested arch.
if "_%VisualStudioVersion%_" == "_15.0_" if "_%VSCMD_ARG_TGT_ARCH%_"=="_%platform%_" goto found_vs2017
set "VSINSTALLDIR="
call deps\libuv\tools\vswhere_usability_wrapper.cmd
if "_%VCINSTALLDIR%_" == "__" goto error-need-vs2017
@rem Need to clear VSINSTALLDIR for vcvarsall to work as expected.
@rem Keep current working directory after call to vcvarsall
set "VSCMD_START_DIR=%CD%"
set vcvars_call="%VCINSTALLDIR%\Auxiliary\Build\vcvarsall.bat" %platform%
echo calling: %vcvars_call%
call %vcvars_call%
if "_%VisualStudioVersion%_" == "_15.0_" if "_%VSCMD_ARG_TGT_ARCH%_"=="_%platform%_" goto found_vs2017
goto error-need-vs2017

:found_vs2017
echo Found MSVC version %VisualStudioVersion%

@rem Check if VS build env is available
if not defined VCINSTALLDIR goto error-need-msbuild
if not defined WindowsSDKDir goto error-need-msbuild

@rem Check Windows SDK version
if "_%WINDOWSSDKLIBVERSION%_" NEQ "_%winsdk%\_" goto error-wrong-winsdk

@rem Check for Python
if not defined PYTHON set PYTHON=python
@"%PYTHON%" --version
if errorlevel 1 goto error-need-python /b 1

echo.
echo Start build...


@rem Build libhandler
echo Build Libhandler... for %platform%, %vsplatform%
echo msbuild deps/libhandler/ide/msvc/libhandler.vcxproj /t:%target% /p:Configuration=%config% /p:Platform="%vsplatform%" /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
msbuild deps/libhandler/ide/msvc/libhandler.vcxproj /t:%target% /p:Configuration=%config% /p:Platform="%vsplatform%" /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
if errorlevel 1 goto error-build /b 1

@rem Build libuv
echo.
echo Build LibUV...
call deps/libuv/vcbuild.bat %platform% %config% vs2017 static
cd %curdir%
if errorlevel 1 goto error-build /b 1

@rem Build zlib
echo.
echo Build ZLib...
set zlib-config=%config%
if "_%zlib-config%_"=="_Release_" set zlib-config=ReleaseWithoutAsm
echo msbuild deps/zlib/contrib/vstudio/vc14/zlibstat.vcxproj /t:%target% /p:Configuration=%zlib-config% /p:Platform="%vsplatform%" /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
msbuild deps/zlib/contrib/vstudio/vc14/zlibstat.vcxproj /t:%target% /p:Configuration=%zlib-config% /p:Platform="%vsplatform%" /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
if errorlevel 1 goto error-build /b 1

@rem Build mbedTLS
echo.
echo Build mbedTLS...
copy /Y deps\mbedTLS-vs2017.vcxproj deps\mbedtls\visualc\VS2010
echo msbuild deps/mbedtls/visualc/VS2010/mbedTLS-vs2017.vcxproj /t:%target% /p:Configuration=%config% /p:Platform="%vsplatform%" /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
msbuild deps/mbedtls/visualc/VS2010/mbedTLS-vs2017.vcxproj /t:%target% /p:Configuration=%config% /p:Platform="%vsplatform%" /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
if errorlevel 1 goto error-build /b 1


@rem Build NodeC
echo Build NodeC...
msbuild ide/msvc/nodec.vcxproj /t:%target% /p:Configuration=%config% /p:Platform="%vsplatform%" /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
if errorlevel 1 goto error-build /b 1


:done
echo.
echo Success!
echo NodeC library generated as : "out/msvc-%vsplatform%/nodecx/%config%/nodecx.lib"
echo NodeC includes are found in: "out/msvc-%vsplatform%/nodecx/inc"
echo done.
goto end

:error-need-vs2017
echo ERROR: cannot find Visual Studio 2017. 
echo You can install the free community edition from: https://visualstudio.microsoft.com/downloads
goto end

:error-need-msbuild
echo ERROR: cannot find msbuild, this file needs to run from the Visual Studio Developer command prompt.
goto end

:error-need-python
echo ERROR: cannot find Python -- it is needed to build libuv.
echo Either set the PYTHON environment variable, or
echo Install Python 2.7 from: https://www.python.org/downloads/release/python-2715
goto end

:error-wrong-winsdk
echo ERROR: Wrong Windows SDK version! Found "%WINDOWSSDKLIBVERSION%", but need "%winsdk%\".
echo Try "https://blogs.msdn.microsoft.com/chuckw/2018/05/02/windows-10-april-2018-update-sdk/" to upgrade the Windows SDK?
goto end

:error-build
echo ERROR: Build error.
goto end

:help
echo msvc-build.bat [debug/release] [x86/x64]
echo Examples:
echo   msvc-build.bat            : builds debug x64 
echo   msvc-build.bat release x86: builds 32bit release build 
goto end


:end
