#!/bin/bash
# Copyright 2018, Microsoft Research, Daan Leijen
# Merging static libraries using GNU ar or MacOSX libtool

echo "--- Merging libraries ---"
curdir="`pwd`"
targetdir="$1"
shift

library="$1"
echo "create library: $library"
shift

macosx=""
case "$OSTYPE" in
  darwin*) macosx="true";;
esac

if test "$macosx" = "true"; then
  # use libtool on MacOSX
  echo "libtool combining: $*"
  libtool -static -o $library $*
else
  # otherwise assume we have GNU ar and combine using a MRI script
  mri="$targetdir/$library.mri"

  echo "create $library" > "$mri"

  # Parse command-line arguments
  while : ; do
    case "$1" in
      "") break;;
      *) echo "add library: $1"
         echo "addlib $1" >> "$mri";;
    esac
    shift
  done

  echo "save" >> "$mri"
  echo "end"  >> "$mri"

  # and run ar on the script
  echo "--- Invoking ar to merge..."
  ar -M < "$mri"
  rm "$mri"
fi

# move the library to the target directory  
mv -f "$library" "$targetdir"

echo "--- Done merging"

#cd "$curdir"