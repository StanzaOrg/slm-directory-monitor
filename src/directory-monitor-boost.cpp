#include <map>
#include <set>
#include <boost/filesystem.hpp>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <iostream>
#include <stanza.h>

using namespace std;
using namespace boost::filesystem;

const bool debug = false;

typedef vector<path> vec;
typedef struct {
    path dir_path;
    map<path, uintmax_t>modification_times;
    set<path>files;
    int is_recurse;
} MonitorContext;

extern "C" {
MonitorContext* directory_monitor_init(const stz_byte* dir_path_str, int is_recurse, int is_add_entries);
stz_byte* directory_monitor_check(MonitorContext* ctx);     // caller needs to free the string after use
void directory_monitor_uninit(MonitorContext* ctx);
}

void add_entries(MonitorContext* ctx, const path& dir_path) {
    vec v;                                // so we can sort them later
    copy(directory_iterator(dir_path), directory_iterator(), back_inserter(v));
    for (vec::const_iterator itr(v.begin()); itr != v.end(); ++itr) {
        const path& entry_path = *itr;
        bool f = is_regular_file(entry_path);
        bool d = is_directory(entry_path) && ctx->is_recurse;
        if (f || d) {
            if (debug) cout << "add " << entry_path << endl;
            ctx->files.insert(path(entry_path));
            ctx->modification_times[entry_path] = last_write_time(entry_path);
        }
        if (d) {
            add_entries(ctx, entry_path);
        }
    }
}

MonitorContext* directory_monitor_init(const stz_byte* dir_path_str, int is_recurse, int is_add_entries) {
    path dir_path = path((const char *)dir_path_str);
    if (!is_directory(dir_path)) {
        return NULL;
    }
    MonitorContext* ctx = new MonitorContext;
    ctx->dir_path = dir_path;
    ctx->is_recurse = is_recurse;
    if (is_add_entries) {
        if (debug) cout << "about to add_entries" << endl;
        add_entries(ctx, dir_path);
    }
    if (debug) cout << "directory_monitor cpp: after init" << endl;
    return ctx;
}

const string check_entries(MonitorContext* ctx, const path& dir_path) {
    try {
        if (is_directory(dir_path)) {
            vec v;
            copy(directory_iterator(dir_path), directory_iterator(), back_inserter(v));
            for (vec::const_iterator itr(v.begin()); itr != v.end(); ++itr) {
                try {
                    const path& entry_path = *itr;
                    bool f = is_regular_file(entry_path);
                    bool d = is_directory(entry_path) && ctx->is_recurse;
                    if (f || d) {
                        auto it = ctx->modification_times.find(entry_path);
                        if (it == ctx->modification_times.end()) {
                            // File has been added
                            if (debug) cout << entry_path << " is added." << endl;
                            ctx->files.insert(entry_path);
                            ctx->modification_times[entry_path] = last_write_time(entry_path);
                            return entry_path.generic_string();
                        } else if (it->second != last_write_time(entry_path)) {
                            // File has been modified
                            if (debug) cout << entry_path << " is modified." << endl;
                            ctx->modification_times[entry_path] = last_write_time(entry_path);
                            return entry_path.generic_string();
                        }
                    }
                    if (d) {
                        auto result = check_entries(ctx, entry_path);
                        if (result != string("")) return result;
                    }
                } catch (filesystem_error e) {
                    if (debug) cout << e.what() << endl;
                }
            }
            return string("");
        } else {
            // the whole directory is destroyed
            // this is a change
            if (debug) cout << "path " << dir_path.generic_string() << " is destroyed." << endl;
            return dir_path.generic_string();
        }
    } catch (filesystem_error e) {
        if (debug) cout << e.what() << endl;
    }
    return string("");
}

const string check_delete(MonitorContext* ctx) {
    // Check for deleted files
    for (auto it = ctx->files.begin(); it != ctx->files.end(); ++it) {
        try {
            if (!exists(*it)) {
                string deleted_filename = (*it).generic_string();
                if (debug) cout << deleted_filename << " is deleted." << endl;
                ctx->files.erase(*it);
                ctx->modification_times.erase(*it);
                return deleted_filename;
            }
        } catch (filesystem_error e) {
            string deleted_filename = (*it).generic_string();
            if (debug) cout << e.what() << endl;
            ctx->files.erase(*it);
            ctx->modification_times.erase(*it);
            return deleted_filename;
        }
    }
    return string("");
}

// caller needs to free the string after use.
stz_byte* directory_monitor_check(MonitorContext* ctx) {
    auto result = check_delete(ctx);
    if (result == string("")) {
        result = check_entries(ctx, ctx->dir_path);
        if (result == string("")) return NULL;
    }
    const char * f_str = result.c_str();
    char* target_str = (char*)malloc(strlen(f_str) + 1);
    strcpy(target_str, f_str);
    if (debug) cout << "directory_monitor cpp: after strcpy " << target_str << endl;
    return (stz_byte*)target_str;
}

void directory_monitor_uninit(MonitorContext* ctx) {
    delete ctx;
    if (debug) cout << "directory_monitor cpp: after uninit" << endl;
}
