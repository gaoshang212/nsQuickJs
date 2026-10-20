# Security Policy

## Supported versions

Security fixes target the latest published release. Before the first release,
report issues against the current `main` branch. Older releases do not have a
separate maintenance commitment.

## Reporting a vulnerability

On this project's GitHub repository, open **Security → Advisories → Report a
vulnerability** to send a private report when private vulnerability reporting is
enabled. Include the affected version or commit, Windows and NSIS versions,
installer architecture, a minimal reproduction, and the expected security impact.

If the private reporting option is unavailable, open an issue asking only for a
private security contact. Do not include exploit details or sensitive data in
that public issue. Wait for the maintainer to provide a private channel.

For ordinary bugs, use the bug report template. This project does not promise a
fixed response time.

## Security boundary

The plugin executes trusted JavaScript inside the installer process. It is not
a sandbox for hostile code. File access is disabled by default and controlled by
the NSIS host; authorized scripts can modify or delete files within `rootDir`.
Concurrent changes to directory links by another process are outside the path
restriction guarantee. JavaScript deadlines cannot interrupt blocking file I/O.

Pass external data through JSON arguments to `Call`, rather than concatenating
it into JavaScript source. See the [API reference](docs/api.md) for details.
