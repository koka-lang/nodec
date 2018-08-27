SETLOCAL ENABLEEXTENSIONS
@ECHO OFF
SET PLATFORM=%1
SET CONFIG=%2

ECHO Platform: %PLATFORM%
ECHO Configuration: %CONFIG%

SET XPLATFORM=""
SET XCONFIG=""

IF "%PLATFORM%"=="x64"   SET ZPLATFORM=x64
IF "%PLATFORM%"=="Win32" SET ZPLATFORM=x86
IF "%CONFIG%"=="Debug"   SET ZCONFIG=Debug
IF "%CONFIG%"=="Release" SET ZCONFIG=ReleaseWithoutAsm
IF "XCONFIG"=="" GOTO ERROR
IF "XPLATFORM"=="" GOTO ERROR

SET ROOT=.\..\..\deps

SET LIBUV=%ROOT%\libuv\%CONFIG%\lib\libuv.lib
IF NOT EXIST %LIBUV% (
  ECHO %LIBUV% does not exist
  GOTO ERROR
)

SET MBEDTLS=%ROOT%\mbedtls\visualc\VS2010\%PLATFORM%\%CONFIG%\mbedtls-vs2017.lib
IF NOT EXIST %MBEDTLS% (
   ECHO %MBEDTLS% does not exist
   GOTO ERROR
)

SET ZLIB=%ROOT%\zlib\contrib\vstudio\vc14\%ZPLATFORM%\ZlibStat%ZCONFIG%\zlibstat.lib
IF NOT EXIST %ZLIB% (
  ECHO %ZLIB% does not exist
  GOTO ERROR
)

SET LIBHANDLER=%ROOT%\libhandler\out\msvc-%PLATFORM%\libhandler\%CONFIG%\libhandler.lib
IF NOT EXIST %LIBHANDLER% (
  ECHO %LIBHANDLER% does not exist
  GOTO ERROR
)

SET NODEC=.\..\..\out\msvc-%PLATFORM%\nodec\%CONFIG%\nodec.lib
IF NOT EXIST %NODEC% (
  ECHO %NODEC% does not exist
  GOTO ERROR
)

SET NODECX_LIBDIR=.\..\..\out\msvc-%PLATFORM%\nodecx\%CONFIG%\lib
IF NOT EXIST %NODECX_LIBDIR% (
  ECHO Creating %NODECX_LIBDIR%
  MKDIR %NODECX_LIBDIR%
)

REM Create combined library as nodecx
SET LIBS=%LIBUV% %ZLIB% %LIBHANDLER% %MBEDTLS% %NODEC%
SET NODECX_LIB=%NODECX_LIBDIR%\nodecx.lib
@ECHO Creating %NODECX_LIB% from %LIBS%
lib /OUT:%NODECX_LIB% %LIBS%
IF NOT ERRORLEVEL 0 GOTO ERROR

REM Copy pdb files for debugging
IF "%CONFIG%"=="Debug" (  
  FOR %%A in (%LIBS%) DO (
    IF "%%~nA"=="libuv" (
      @ECHO copy /Y %%~dpA..\libuv.pdb %%~dpAlibuv.pdb
      COPY /Y %%~dpA..\libuv.pdb %%~dpAlibuv.pdb
    )
    @ECHO copy /Y %%~dpnA.pdb %NODECX_LIBDIR%
    @COPY /Y %%~dpnA.pdb %NODECX_LIBDIR%
  )
)


SET NODECX_INCDIR=.\..\..\out\msvc-%PLATFORM%\nodecx\%CONFIG%\include
IF NOT EXIST %NODECX_INCDIR% (
  ECHO Creating %NODECX_INCDIR%
  MKDIR %NODECX_INCDIR%
  ECHO Creating %NODECX_INCDIR%\libuv\include\uv
  MKDIR %NODECX_INCDIR%\libuv\include\uv
  ECHO Creating %NODECX_INCDIR%\http-parser
  MKDIR %NODECX_INCDIR%\http-parser
  ECHO Creating %NODECX_INCDIR%\libhandler\inc
  MKDIR %NODECX_INCDIR%\libhandler\inc
  ECHO Creating %NODECX_INCDIR%\mbedtls
  MKDIR %NODECX_INCDIR%\mbedtls
)

@ECHO Collecting include files to %NODECX_INCDIR%
COPY /Y %ROOT%\libuv\include\*.h  %NODECX_INCDIR%\libuv\include
COPY /Y %ROOT%\libuv\include\uv\*.h  %NODECX_INCDIR%\libuv\include\uv
REM COPY %ROOT%\mbedtls\include\mbedtls\*.h %INCDIR%\mbedtls
COPY /Y %ROOT%\http-parser\http_parser.h %NODECX_INCDIR%\http-parser
COPY /Y %ROOT%\libhandler\inc\libhandler.h %NODECX_INCDIR%\libhandler\inc
COPY /Y .\..\..\inc\nodec.h %NODECX_INCDIR%
COPY /Y .\..\..\inc\nodec-primitive.h %NODECX_INCDIR%


ENDLOCAL
GOTO END
:ERROR
ENDLOCAL
SET ERRORLEVEL=1
:END
