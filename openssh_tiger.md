# Building OpenSSH for Tiger with Modern Crypto

Tiger's OpenSSH 4.5 has critical vulnerabilities. We'll build OpenSSH 9.x with LibreSSL.

## Why LibreSSL instead of OpenSSL?

- LibreSSL is a fork focused on security and portability
- Better support for legacy systems
- Cleaner codebase, easier to build on Tiger
- Still maintained with security updates

## Build Plan

### 1. Build LibreSSL 3.8.x (latest portable)
```bash
curl -L -o libressl.tar.gz https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-3.8.2.tar.gz
tar xzf libressl.tar.gz
cd libressl-3.8.2
./configure --prefix=/usr/local/libressl \
    CC="gcc -arch ppc" \
    CFLAGS="-O2 -mcpu=7450"
make -j2
sudo make install
```

### 2. Build OpenSSH 9.x
```bash
curl -L -o openssh.tar.gz https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-9.6p1.tar.gz
tar xzf openssh.tar.gz
cd openssh-9.6p1
./configure --prefix=/usr/local \
    --with-ssl-dir=/usr/local/libressl \
    --with-privsep-path=/var/empty \
    --with-privsep-user=sshd \
    CC="gcc -arch ppc" \
    CFLAGS="-O2 -mcpu=7450"
make -j2
sudo make install
```

### 3. Update System SSH
```bash
# Backup old SSH
sudo mv /usr/bin/ssh /usr/bin/ssh.old
sudo mv /usr/bin/scp /usr/bin/scp.old
sudo mv /usr/sbin/sshd /usr/sbin/sshd.old

# Link new SSH
sudo ln -s /usr/local/bin/ssh /usr/bin/ssh
sudo ln -s /usr/local/bin/scp /usr/bin/scp
sudo ln -s /usr/local/sbin/sshd /usr/sbin/sshd
```

## CVEs Fixed by Upgrading

- CVE-2016-0777 - Roaming buffer overflow
- CVE-2016-0778 - Roaming buffer overflow
- CVE-2015-5600 - Keyboard-interactive bypass
- CVE-2014-2532 - SSHFP DNS record validation
- Many more...

## Notes

- Tiger's /var/empty may need to be created
- sshd user may need to be created
- Host keys will need regeneration
