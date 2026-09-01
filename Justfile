image_name := "localhost/krunner-bazaar:dev"
default: build

build-container:
    #!/usr/bin/env bash
    echo "Building development container image..."
    podman build -t {{image_name}} -f Containerfile .

build: build-container
    #!/usr/bin/env bash
    set -euo pipefail

    echo "Building krunner-bazaar..."

    # Create build directory if it doesn't exist
    mkdir -p build

    # Build using development container
    podman run --rm \
        --userns=keep-id \
        --volume "$(pwd):/workspace:Z" \
        --workdir /workspace \
        {{image_name}} \
        bash -c '
            set -euo pipefail
            # Configure and build the project
            cd build
            cmake .. \
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release} \
                -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX:-/usr}

            make -j$(nproc)
        '

# This should work on Aurora/Bazzite
smoke: build
    #!/usr/bin/env bash
    set -euo pipefail

    ./build/bin/bazaar-dbus-tool --search "KolourPaint"
    ./build/bin/bazaar-dbus-tool --activate org.kde.kolourpaint


gdb:
    gdb -ex "run" -ex "bt" --args ./build/bin/bazaar-dbus-tool -s spotify

rpm: build-container
    #!/usr/bin/env bash
    set -euo pipefail
    # Get project version from CMakeLists.txt or use default
    VERSION=${VERSION:-1.0.0}

    # Create RPM build structure
    mkdir -p rpmbuild/{SOURCES,SPECS,BUILD,RPMS,SRPMS}

    # Create source tarball
    echo "Creating source tarball..."
    git archive --format=tar.gz --prefix=krunner-bazaar-${VERSION}/ HEAD > rpmbuild/SOURCES/krunner-bazaar-${VERSION}.tar.gz

    # Copy spec file
    cp krunner-bazaar.spec rpmbuild/SPECS/

    # Build RPM using development container
    podman run --rm \
        --userns=keep-id \
        --volume "$(pwd):/workspace" \
        --workdir /workspace \
        {{image_name}} \
        bash -c "
            set -euo pipefail

            # Build the RPM
            rpmbuild --define '_topdir /workspace/rpmbuild' \
                     --define 'version ${VERSION}' \
                     -ba rpmbuild/SPECS/krunner-bazaar.spec
        "

    echo "RPM build complete!"
    echo "Built packages:"
    find rpmbuild/RPMS -name "*.rpm" -type f
    find rpmbuild/SRPMS -name "*.rpm" -type f

bump-version version:
    #!/usr/bin/env bash
    set -euo pipefail

    VERSION="{{version}}"

    # Validate version format (should be like 1.2.3)
    if ! echo "$VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
        echo "Error: Version must be in format X.Y.Z (e.g., 1.2.3)"
        exit 1
    fi

    echo "Bumping version to $VERSION..."

    # Check if working directory is clean
    if ! git diff --quiet || ! git diff --cached --quiet; then
        echo "Error: Working directory is not clean. Please commit or stash changes first."
        git status --porcelain
        exit 1
    fi

    # Update version in bazaarrunner.json
    echo "Updating version in src/bazaarrunner.json..."
    sed -i "s/\"Version\": \"[^\"]*\"/\"Version\": \"$VERSION\"/" src/bazaarrunner.json

    # Verify the change was made
    if ! grep -q "\"Version\": \"$VERSION\"" src/bazaarrunner.json; then
        echo "Error: Failed to update version in bazaarrunner.json"
        exit 1
    fi

    echo "Version updated successfully in bazaarrunner.json"
    git diff src/bazaarrunner.json
    git add src/bazaarrunner.json
    git commit -m "chore: bump version to v$VERSION"
    git push origin HEAD

    git tag "v$VERSION"
    git push origin "v$VERSION"
