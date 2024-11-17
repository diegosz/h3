# Development Notes

- [Development Notes](#development-notes)
  - [Generate the `ch3` Package](#generate-the-ch3-package)
    - [Clone repository](#clone-repository)
    - [Install Tools](#install-tools)
    - [Preprocess](#preprocess)
    - [Patch Code to Build on Linux](#patch-code-to-build-on-linux)
    - [Initialize Go Module](#initialize-go-module)
    - [Transpile](#transpile)
    - [Fix Issues](#fix-issues)
    - [Test](#test)
    - [Ready](#ready)
  - [Versioning](#versioning)
  - [Resources](#resources)

This is a fork of the original [`h3`](https://github.com/uber/h3) package.

[H3](https://github.com/uber/h3/) is a geospacial indexing library created by Uber similar to [S2](https://s2geometry.io/).

Uber is providing [h3 for Go](https://github.com/uber/h3-go).
Unfortunately it’s a CGO version, a C to Go binding as you may know [CGO is not Go](https://dave.cheney.net/2016/01/18/cgo-is-not-go).

Having a native Go library is better, specially for running on Docker or Kubernetes without CGO.

These is based on the [`goh3`](https://github.com/akhenakh/goh3) package.

We will generate an updated `ch3` package that will be used in the [`goh3`](https://github.com/diegosz/goh3) fork package.

## Generate the `ch3` Package

### Clone repository

```shell
git clone git@github.diegos_exo:uber/h3.git
```

### Install Tools

Install [`ccgo`](https://gitlab.com/cznic/ccgo) V3:

```shell
bingo get -l modernc.org/ccgo/v3@latest --name ccgo
```

### Preprocess

The file `h3api.h.in` is preprocessed into the file `h3api.h` as part of H3's build process.
The preprocessing inserts the correct values for the `H3_VERSION_MAJOR`, `H3_VERSION_MINOR`, and `H3_VERSION_PATCH` macros.

Don't know how to do the preprocessing yet, it seems that we need to run `./configure`, but there might be many dependencies that will make our life miserable.

It seems that it only changes the configuration variables with a syntax `@VARIABLE_NAME@` and the only definitions that get configured by the preprocessor are these:

```h
#define H3_VERSION_MAJOR @H3_VERSION_MAJOR@
#define H3_VERSION_MINOR @H3_VERSION_MINOR@
#define H3_VERSION_PATCH @H3_VERSION_PATCH@
```

For the moment we could copy the file `h3api.h.in` to `h3api.h` and set the values manually.

```shell
cp src/h3lib/include/h3api.h.in src/h3lib/include/h3api.h
sed -i 's/@H3_VERSION_MAJOR@/4/g' src/h3lib/include/h3api.h
sed -i 's/@H3_VERSION_MINOR@/1/g' src/h3lib/include/h3api.h
sed -i 's/@H3_VERSION_PATCH@/0/g' src/h3lib/include/h3api.h
```

Remove `src/h3lib/include/h3api.h` from `.gitignore`:

```shell
sed -i '/src\/h3lib\/include\/h3api.h/d' .gitignore
```

### Patch Code to Build on Linux

Patch the original source to avoid builtin `isfinite` issue (C tests are passing).

Create `src/h3lib/include/isfinite.h` file:

```h
// isfinite.h
#ifndef ISFINITE_H
#define ISFINITE_H

#include <stdbool.h>

bool isXfinite(double f);

#endif // ISFINITE_H
```

Create `src/h3lib/lib/isfinite.c` file:

```c
#include "isfinite.h"

bool isXfinite(double f) { return !isnan(f - f); }
```

Edit `src/h3lib/lib/bbox.c` AND `src/h3lib/lib/h3Index.c` files to include the `isfinite.h` header and replace all `isfinite` with `isXfinite`:

```c
#include "isfinite.h"

/**
 * Encodes a coordinate on the sphere to the H3 index of the containing cell at
 * the specified resolution.
 *
 * Returns 0 on invalid input.
 *
 * @param g The spherical coordinates to encode.
 * @param res The desired H3 resolution for the encoding.
 * @return The encoded H3Index (or H3_NULL on failure).
 */
H3Index H3_EXPORT(geoToH3)(const GeoCoord* g, int res) {
    if (res < 0 || res > MAX_H3_RES) {
        return H3_NULL;
    }
    if (!isXFinite(g->lat) || !isXFinite(g->lon)) {
        return H3_NULL;
    }

    FaceIJK fijk;
    _geoToFaceIjk(g, res, &fijk);
    return _faceIjkToH3(&fijk, res);
}
```

### Initialize Go Module

```shell
go mod init ch3
go get -u modernc.org/libc
go get -u modernc.org/libc/sys/types
go mod tidy
```

Add helper functions in `helper.go`:

```go
package ch3

import (
 "math"

 "modernc.org/libc"
)

func Xfmin(tls *libc.TLS, a, b float64) float64 {
 return math.Min(a, b)
}

func Xfmax(tls *libc.TLS, a, b float64) float64 {
 return math.Max(a, b)
}

func Xlround(tls *libc.TLS, a float64) float64 {
 return math.Round(a)
}
```

### Transpile

There is a recent effort to transpile C to Go using [ccgo](https://pkg.go.dev/modernc.org/ccgo/v3) and the associated [libc](https://pkg.go.dev/modernc.org/libc).

`Sqlite` has been [transpiled using this technique](https://pkg.go.dev/modernc.org/sqlite#section-readme) with great success; more recently [PCRE2](https://www.reddit.com/r/golang/comments/v0sdn2/a_cgofree_port_of_the_pcre2_regular_expression/) as well.

The result library is often slower than pure C (~2x magnitude) but still having a native Go port could simplify some aspects of the workflow like testing.

Not 100% of the `libc` is covered, some builtins are not defined, for example `isFinite`.

For transpiling we need to use the `ccgo` tool:

```shell
CC=/usr/bin/gcc ccgo -pkgname ch3 -trace-translation-units -export-externs X -export-defines D -export-fields F -export-structs S -export-typedefs T -Isrc/h3lib/include src/h3lib/lib/*.c

go mod tidy
```

### Fix Issues

Manually fix issues in the generated code, basically replace some `uint32` into `int32` in the generated code.

Check the commit for the changes.

### Test

```shell
go test -v
```

### Ready

We could now copy the `a_linux_amd64.go` and `capi_linux_amd64.go` into the`ch3` package folder in our Go code.

## Versioning

In the local clone, after pulling commits and tags from upstream and reapplying the fork changes and testing, we need to apply a tag that is `semver` superior but also that is not going to have conflicts with any future upstream tag.

Use tags with the following structure:

```text
AAA = 3 digit 0 padded UPSTREAM_MAYOR

BBB = 3 digit 0 padded UPSTREAM_MINOR

CCC = 3 digit 0 padded UPSTREAM_PATCH

DDD = 3 digit 0 padded incremental counter starting in 1

FORK_TAG = <UPSTREAM_PATCH><AAA><BBB><CCC><DDD>

v<UPSTREAM_MAYOR>.<UPSTREAM_MINOR>.<FORK_TAG>

v<UPSTREAM_MAYOR>.<UPSTREAM_MINOR>.<FORK_TAG>-pre

v<UPSTREAM_MAYOR>.<UPSTREAM_MINOR>.<FORK_TAG>+build

v<UPSTREAM_MAYOR>.<UPSTREAM_MINOR>.<FORK_TAG>-pre+build
```

For example:

| upstream | fork              | issue |
| -------- | ----------------- | ----- |
| 2.3.1    |                   |       |
|          | 2.3.1002003001001 | ok    |
| 2.3.2    |                   |       |
|          | 2.3.2002003002001 | ok    |
|          | 2.3.2002003002002 | ok    |
|          | 2.3.2002003002003 | ok    |
| 2.4.1    |                   |       |
|          | 2.4.1002004001001 | ok    |
| 3.3.1    |                   |       |
|          | 3.3.1003003001001 | ok    |

Invalid options:

| upstream | fork      | issue                                                 |
| -------- | --------- | ----------------------------------------------------- |
| 2.3.1    |           |                                                       |
|          | 2.3.2     | future conflict with upstream tag                     |
|          | 2.4.1     | worst future conflict with upstream tag               |
|          | 3.3.1     | even worst future conflict with upstream tag          |
|          | 2.3.1-001 | is a semver pre-release so it's not picked for update |
|          | 2.3.1+001 | the build meta tag it's not picked for update         |
| 2.3.2    |           |                                                       |
| 2.4.1    |           |                                                       |
| 3.3.1    |           |                                                       |

## Resources

- [GitHub - akhenakh/goh3: A native Go h3 port](https://github.com/akhenakh/goh3)
- [GitHub - uber/h3: Hexagonal hierarchical geospatial indexing system](https://github.com/uber/h3)
- [Surprising result while transpiling C to Go - Fabrice Aneche](https://blog.nobugware.com/post/2022/surprising-result-while-transpiling-go/)
