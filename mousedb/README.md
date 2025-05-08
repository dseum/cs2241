# mousedb

This is the database library. The focus of this project is only on features related to the database such as parsing, querying, and storage.

```cpp
#include <mousedb/database/core.hpp>

using namespace mousedb::database;

int main() {
    Database db("/var/lib/mousedb");
    return 0;
}
```

### Development

```bash
docker compose up --build -d
docker compose attach shell
```

- To build, run `cmake --workflow build-<target>`
- To build and test, run `cmake --workflow test-<target>`
- To test, run `ctest --preset <target>`
- To bench, build and run `build/exe/bench`

