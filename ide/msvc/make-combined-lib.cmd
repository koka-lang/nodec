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
  GOTO ERROR
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
SET COMBINED=%COMBINED_DIR%\nodecx.lib
@ECHO Creating %COMBINED%
lib /OUT:%COMBINED%  %LIBUV% %MBEDTLS% %ZLIB% %LIBHANDLER% %NODEC%
IF NOT ERRORLEVEL 0 GOTO ERROR


SET INCDIR=.\..\..\out\msvc-%PLATFORM%\nodecx\inc
IF NOT EXIST %INCDIR% (
  ECHO Creating %INCDIR%
  MKDIR %INCDIR%
  ECHO Creating %INCDIR%\libuv\include
  MKDIR %INCDIR%\libuv\include
  ECHO Creating %INCDIR%\mbedtls
  MKDIR %INCDIR%\mbedtls
)

@ECHO Collecting include files to %INCDIR%
COPY %ROOT%\libuv\include\*.h  %INCDIR%\libuv\include
COPY %ROOT%\mbedtls\include\mbedtls\*.h %INCDIR%\mbedtls
COPY %ROOT%\http-parser\http_parser.h %INCDIR%
COPY %ROOT%\libhandler\inc\libhandler.h %INCDIR%
COPY %ROOT%\nodec\inc\nodec.h %INCDIR%
COPY %ROOT%\nodec\inc\nodec-primitive.h %INCDIR%


ENDLOCAL
GOTO END
:ERROR
ENDLOCAL
SET ERRORLEVEL=1
:END
