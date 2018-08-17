SETLOCAL ENABLEEXTENSIONS
@ECHO OFF
SET PLATFORM=%1
SET CONFIG=%2

ECHO Platform: %PLATFORM%
ECHO Configuration: %CONFIG%

SET XPLATFORM=""
SET XCONFIG=""

IF "%PLATFORM%"=="x64"   SET ZPLATFORM=x64
IF "%PLATFORM%"=="x64"   SET MPLATFORM=build64
IF "%PLATFORM%"=="Win32" SET ZPLATFORM=x86
IF "%PLATFORM%"=="Win32" SET MPLATFORM=build32
IF "%CONFIG%"=="Debug"   SET ZCONFIG=Debug
IF "%CONFIG%"=="Release" SET ZCONFIG=ReleaseWithoutAsm
IF "XCONFIG"=="" GOTO ERROR
IF "XPLATFORM"=="" GOTO ERROR

SET ROOT=.\..\..\..

SET LIBUV=%ROOT%\libuv\%CONFIG%\lib\libuv.lib
IF NOT EXIST %LIBUV% (
  ECHO %LIBUV% does not exist
  GOTO ERROR
)

SET MBEDTLS=%ROOT%\mbedtls\builds\%MPLATFORM%\library\%CONFIG%\mbedTLS.lib
IF NOT EXIST %MBEDTLS% (
   ECHO %MBEDTLS% does not exist
REM  GOTO ERROR
)

SET ZLIB=%ROOT%\zlib\contrib\vstudio\vc14\%ZPLATFORM%\ZlibStat%ZCONFIG%\zlibstat.lib
IF NOT EXIST %ZLIB% (
  ECHO %ZLIB% does not exist
  GOTO ERROR
)

SET LIBHANDLER=.\..\..\out\msvc-%PLATFORM%\libhandler\%CONFIG%\libhandler.lib
IF NOT EXIST %LIBHANDLER% (
  ECHO %LIBHANDLER% does not exist
  GOTO ERROR
)

SET NODEC=.\..\..\out\msvc-%PLATFORM%\nodec\%CONFIG%\nodec.lib
IF NOT EXIST %NODEC% (
  ECHO %NODEC% does not exist
  GOTO ERROR
)

SET COMBINED_DIR=.\..\..\out\msvc-%PLATFORM%\nodecx\%CONFIG%
IF NOT EXIST %COMBINED_DIR% (
  ECHO Creating %COMBINED_DIR%
  MKDIR %COMBINED_DIR%
)

REM Create combined library as nodecx
REM SET LIBS=%LIBUV% %ZLIB% %LIBHANDLER% %MBEDTLS% %NODEC%
SET LIBS=%LIBUV% %ZLIB% %LIBHANDLER% %NODEC%
SET COMBINED=%COMBINED_DIR%\nodecx.lib
@ECHO Creating %COMBINED% from %LIBS%
lib /OUT:%COMBINED% %LIBS%
IF NOT ERRORLEVEL 0 GOTO ERROR

REM Copy pdb files for debugging
IF "%CONFIG%"=="Debug" (  
  FOR %%A in (%LIBS%) DO (
    IF "%%~nA"=="libuv" (
      @ECHO copy /Y %%~dpA..\libuv.pdb %%~dpAlibuv.pdb
      COPY /Y %%~dpA..\libuv.pdb %%~dpAlibuv.pdb
    )
    @ECHO copy /Y %%~dpnA.pdb %COMBINED_DIR%
    @COPY /Y %%~dpnA.pdb %COMBINED_DIR%
  )
)


SET INCDIR=.\..\..\out\msvc-%PLATFORM%\nodecx\inc
IF NOT EXIST %INCDIR% (
  ECHO Creating %INCDIR%
  MKDIR %INCDIR%
  ECHO Creating %INCDIR%\libuv\include\uv
  MKDIR %INCDIR%\libuv\include\uv
  ECHO Creating %INCDIR%\http-parser
  MKDIR %INCDIR%\http-parser
  ECHO Creating %INCDIR%\libhandler\inc
  MKDIR %INCDIR%\libhandler\inc
  ECHO Creating %INCDIR%\mbedtls
  MKDIR %INCDIR%\mbedtls
)

@ECHO Collecting include files to %INCDIR%
COPY /Y %ROOT%\libuv\include\*.h  %INCDIR%\libuv\include
COPY /Y %ROOT%\libuv\include\uv\*.h  %INCDIR%\libuv\include\uv
REM COPY %ROOT%\mbedtls\include\mbedtls\*.h %INCDIR%\mbedtls
COPY /Y %ROOT%\http-parser\http_parser.h %INCDIR%\http-parser
COPY /Y %ROOT%\libhandler\inc\libhandler.h %INCDIR%\libhandler\inc
COPY /Y %ROOT%\nodec\inc\nodec.h %INCDIR%
COPY /Y %ROOT%\nodec\inc\nodec-primitive.h %INCDIR%


ENDLOCAL
GOTO END
:ERROR
ENDLOCAL
SET ERRORLEVEL=1
:END
