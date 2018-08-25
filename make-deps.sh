#!/bin/bash
# Copyright 2018, Microsoft Research, Daan Leijen
echo "--- Building NodeC dependencies ---"
curdir=`pwd`

build_libhandler="yes"
build_libuv="yes"
build_libz="yes"

# Parse command-line arguments
while : ; do
  flag="$1"
  case "$flag" in
  *=*)  flag_arg="${flag#*=}";;
  *)    flag_arg="yes" ;;
  esac
  # echo "option: $flag, arg: $flag_arg"
  case "$flag" in
    "") break;;    
    --libuv)
        build_libuv="yes";;
    --no-libuv)
        build_libuv="no";;
    --libhandler)
        build_libhandler="yes";;
    --no-libhandler)
        build_libhandler="no";;
    --zlib)
        build_zlib="yes";;
    --no-zlib)
        build_zlib="no";;
    -h|--help|-\?|help|\?)
        echo "./configure [options]"
        echo "  --libuv                        build libuv (default)"
        echo "  --no-libuv                     skip libuv" 
        echo "  --libhandler                   build libhandler (default)"
        echo "  --no-libhandler                skip libhandler" 
        echo "  --zlib                         build zlib (default)"
        echo "  --no-zlib                      skip zlib" 
        exit 0;;
    *) echo "warning: unknown option \"$1\"." 1>&2
  esac
  shift
done
echo "use '--help' for help on configuation options."


if test "$build_libhandler" = "yes"; then
  echo "--- Building Libhandler ---"
  cd deps/libhandler
  if test -f "./out/makefile.config"; then
    echo "found previous out/makefile.config; skip configure"
  else
    echo "--- Libhandler: configure..."
    ./configure
  fi
  echo ""
  echo "--- Libhandler: debug build..."
  make
  echo ""
  echo "--- Libhandler: release build..."
  make VARIANT=release
  echo ""
  echo "--- Libhandler done ---"
  cd "$curdir"
fi

if test "$build_libuv" = "yes"; then
  echo ""
  echo "--- Building LibUV ---"
  cd deps/libuv
  if test -f "./config.status"; then
    echo "found previous config.status; skip configure"
  else
    if test -f "./configure"; then 
      echo "found previous configure; skip autogen.sh"
    else 
      echo "--- LibUV: autogen..."
      ./autogen.sh
    fi
    echo "--- LibUV: configure..."
    config_cmd='./configure --prefix="$curdir/deps/libuv/out" --enable-static --config-cache'
    echo "$config_cmd"
    $config_cmd    
  fi
  echo 
  echo "--- LibUV: release build..."
  make install
  echo ""
  echo "--- LibUV: done ---"
  cd "$curdir"
fi

if test "$build_libz" = "yes"; then
  echo "--- Building ZLib ---"
  cd deps/zlib
  if test -f "./configure.log"; then
    echo "found previous configure.log; skip configure"
  else
    echo "--- ZLib: configure..."
    config_cmd='./configure --prefix="$curdir/deps/zlib/out" --static'
    echo "$config_cmd"
    $config_cmd
  fi
  echo ""
  echo "--- ZLib: release build..."
  make install
  echo ""
  echo "--- ZLib: done ---"
  cd "$curdir"
fi

echo "--- Done building dependencies ---"
