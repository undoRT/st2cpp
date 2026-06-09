#!/bin/bash

# Colori per output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== st2cpp Release Script ===${NC}"

# Leggi versione da CMakeLists.txt
VERSION=$(grep -oP 'project\(st2cpp VERSION \K[^)]+' CMakeLists.txt)
echo -e "Current version: ${GREEN}${VERSION}${NC}"

# Chiedi nuovo numero di versione
read -p "Enter new version (current: ${VERSION}): " NEW_VERSION

if [ -z "$NEW_VERSION" ]; then
    echo -e "${YELLOW}Keeping version ${VERSION}${NC}"
    NEW_VERSION=$VERSION
fi

# Aggiorna CMakeLists.txt
sed -i "s/project(st2cpp VERSION [0-9.]\+)/project(st2cpp VERSION ${NEW_VERSION})/" CMakeLists.txt

# Aggiorna include/version.hpp
cat > include/version.hpp << EOF
#pragma once

#define ST2CPP_VERSION_MAJOR ${NEW_VERSION%%.*}
#define ST2CPP_VERSION_MINOR ${NEW_VERSION#*.}
#define ST2CPP_VERSION_PATCH ${NEW_VERSION##*.}
#define ST2CPP_VERSION "${NEW_VERSION}"
EOF

# Chiedi se è pre-release
read -p "Is this a pre-release? (y/n): " IS_PRERELEASE
if [[ "$IS_PRERELEASE" == "y" ]]; then
    TAG_VERSION="${NEW_VERSION}-beta"
else
    TAG_VERSION="${NEW_VERSION}"
fi

echo -e "${YELLOW}Tag version: ${GREEN}${TAG_VERSION}${NC}"

# Chiedi conferma
read -p "Create release ${TAG_VERSION}? (y/n): " CONFIRM
if [[ "$CONFIRM" != "y" ]]; then
    echo -e "${RED}Aborted.${NC}"
    exit 1
fi

# Commit
git add CMakeLists.txt include/version.hpp
git commit -m "Bump version to ${NEW_VERSION}"

# Tag
git tag -a "v${TAG_VERSION}" -m "Release v${TAG_VERSION}"

# Push
git push origin main
git push origin "v${TAG_VERSION}"

echo -e "${GREEN}Release triggered! Check GitHub Actions.${NC}"