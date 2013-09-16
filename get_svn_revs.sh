#!/bin/sh

set -x
set -e
git config core.notesRef refs/notes/freebsd/commits
git config --add remote.origin.fetch '+refs/notes/*:refs/notes/*'
echo  "This will take a few minutes..."
git fetch --all
