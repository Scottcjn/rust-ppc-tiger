#!/bin/bash
# Git wrapper for Tiger with HTTPS support
# Uses our wget_tiger for downloads, proxies push through NAS
#
# Install:
#   chmod +x git_tiger_wrapper.sh
#   sudo cp git_tiger_wrapper.sh /usr/local/bin/git-https
#
# Usage:
#   git-https clone https://github.com/user/repo.git
#   git-https push origin main

WGET_TIGER="/usr/local/bin/wget"
PROXY_HOST="192.168.0.160"  # Sophia NAS with TLS support
PROXY_PORT="8443"

# Check for our wget
if [ ! -x "$WGET_TIGER" ]; then
    echo "ERROR: wget_tiger not found at $WGET_TIGER"
    echo "Please build and install wget_tiger first"
    exit 1
fi

case "$1" in
    clone)
        # Clone via tarball download (works for public repos)
        URL="$2"

        # Extract owner/repo from URL
        if [[ "$URL" =~ github\.com[/:]([^/]+)/([^/.]+) ]]; then
            OWNER="${BASH_REMATCH[1]}"
            REPO="${BASH_REMATCH[2]}"

            echo "Cloning $OWNER/$REPO via tarball..."

            # Download tarball
            TARBALL_URL="https://github.com/$OWNER/$REPO/archive/refs/heads/main.tar.gz"
            $WGET_TIGER -O "$REPO.tar.gz" "$TARBALL_URL"

            if [ $? -eq 0 ]; then
                # Extract
                tar xzf "$REPO.tar.gz"
                mv "$REPO-main" "$REPO"
                rm "$REPO.tar.gz"

                # Initialize as git repo
                cd "$REPO"
                git init
                git add .
                git commit -m "Initial clone from GitHub"
                git remote add origin "$URL"

                echo ""
                echo "Cloned $REPO successfully!"
                echo "Note: This is a snapshot. Use 'git-https pull' to update."
            else
                echo "Failed to download tarball"
                # Try master branch
                TARBALL_URL="https://github.com/$OWNER/$REPO/archive/refs/heads/master.tar.gz"
                $WGET_TIGER -O "$REPO.tar.gz" "$TARBALL_URL"

                if [ $? -eq 0 ]; then
                    tar xzf "$REPO.tar.gz"
                    mv "$REPO-master" "$REPO"
                    rm "$REPO.tar.gz"
                    cd "$REPO"
                    git init
                    git add .
                    git commit -m "Initial clone from GitHub"
                    git remote add origin "$URL"
                    echo "Cloned $REPO successfully (master branch)!"
                fi
            fi
        else
            echo "ERROR: Could not parse GitHub URL: $URL"
            exit 1
        fi
        ;;

    pull)
        # Pull updates via tarball
        REMOTE=$(git remote get-url origin 2>/dev/null)
        if [ -z "$REMOTE" ]; then
            echo "ERROR: Not in a git repository or no origin remote"
            exit 1
        fi

        if [[ "$REMOTE" =~ github\.com[/:]([^/]+)/([^/.]+) ]]; then
            OWNER="${BASH_REMATCH[1]}"
            REPO="${BASH_REMATCH[2]}"

            # Get current branch
            BRANCH=$(git branch --show-current 2>/dev/null || echo "main")

            echo "Pulling $OWNER/$REPO ($BRANCH) via tarball..."

            TARBALL_URL="https://github.com/$OWNER/$REPO/archive/refs/heads/$BRANCH.tar.gz"
            TMPDIR=$(mktemp -d)

            $WGET_TIGER -O "$TMPDIR/update.tar.gz" "$TARBALL_URL"

            if [ $? -eq 0 ]; then
                cd "$TMPDIR"
                tar xzf update.tar.gz

                # Sync files (excluding .git)
                rsync -av --exclude='.git' "$REPO-$BRANCH/" "$(dirs +1)/"

                cd - > /dev/null
                rm -rf "$TMPDIR"

                echo "Pull complete. Use 'git status' to see changes."
            else
                rm -rf "$TMPDIR"
                echo "Failed to pull updates"
                exit 1
            fi
        fi
        ;;

    push)
        # Push requires the proxy on NAS
        echo "Push requires TLS proxy on NAS ($PROXY_HOST:$PROXY_PORT)"
        echo ""
        echo "Option 1: Use the NAS as git proxy"
        echo "  ssh sophia@$PROXY_HOST 'cd /tmp && git clone --mirror . && git push'"
        echo ""
        echo "Option 2: Create a patch and email it"
        echo "  git format-patch origin/main --stdout > changes.patch"
        echo ""
        echo "Option 3: Use git bundle"
        echo "  git bundle create changes.bundle origin/main..HEAD"
        echo "  # Transfer bundle to modern machine and apply"
        ;;

    *)
        # Pass through to regular git
        git "$@"
        ;;
esac
