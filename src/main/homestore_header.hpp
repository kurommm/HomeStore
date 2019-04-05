#ifndef _HOMESTORE_HEADER_HPP_
#define _HOMESTORE_HEADER_HPP_

#include <boost/uuid/uuid.hpp>
#include <string>

#ifdef _PRERELEASE
#include <flip/flip.hpp>
#endif

namespace homeds {
struct blob {
    uint8_t *bytes;
    uint32_t size;
};
}

namespace homestore {

enum io_flag {
#ifndef NDEBUG
    BUFFERED_IO = 0, // should be set if file system doesn't support direct IOs and we are working on a file as a disk.
                     // This option is enabled only on in debug build.
#endif
    DIRECT_IO = 1  // recommened mode
};

struct dev_info {
    std::string dev_names;
};

#ifdef _PRERELEASE
class HomeStoreFlip {
public:
    static flip::Flip *instance() {
        static flip::Flip inst;
        return &inst;
    }
};

#define homestore_flip HomeStoreFlip::instance()
#endif
}

#endif
