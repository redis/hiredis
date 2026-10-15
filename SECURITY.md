# Security Policy

## Supported Platforms

Hiredis officially supports Unix-like systems (Linux, macOS, BSD) only.

**Windows is not an officially supported platform.** Windows compatibility code has been contributed by
the community and is provided on a best-effort basis. There are known incompatibilities on Windows,
including security vulnerabilities.
Known Windows-specific issues are tracked under the
[`windows-compatibility`](https://github.com/redis/hiredis/issues?q=is%3Aissue%20label%3Awindows-compatibility)
label.

Before reporting a vulnerability, please check whether it can be reproduced on a supported platform.
Issues that only affect Windows may be handled as regular Windows compatibility issues rather than
through the vulnerability disclosure process.

## Reporting a Vulnerability

If you believe you have found a security vulnerability, to ensure proper review and assessment, we kindly ask
vulnerability reports be submitted through the
[Redis Vulnerability Disclosure Program](https://redis.io/redis-responsible-vulnerability-disclosure/).
Alternatively reach out via email to security@redis.com.
