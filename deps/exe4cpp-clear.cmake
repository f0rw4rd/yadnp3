# Patches exe4cpp's MockExecutor: run as the PATCH_COMMAND of its FetchContent, in its source directory.
#
# A pending timer or posted action of the MockExecutor often holds a shared_ptr to the context that holds the
# executor, so a test that ends with one pending leaks both. clear() drops them without running them, which a test
# fixture calls on destruction. The executor's own containers are emptied before the actions are destroyed, so a
# destructor that cancels a timer finds nothing to erase.

function(replace_once path old new)
    file(READ "${path}" content)
    string(FIND "${content}" "${old}" first)
    if(first EQUAL -1)
        message(FATAL_ERROR "exe4cpp-clear: no '${old}' in ${path}")
    endif()
    string(REPLACE "${old}" "${new}" content "${content}")
    file(WRITE "${path}" "${content}")
endfunction()

replace_once("src/exe4cpp/MockExecutor.h"
    "    /**	@return true if an action was run. */\n"
    "    /** Drops every pending timer and posted action without running it */\n    void clear()\n    {\n        timer_vector_t pending_timers;\n        pending_timers.swap(this->timers);\n        post_queue_t pending_posts;\n        pending_posts.swap(this->post_queue);\n    }\n\n    /**	@return true if an action was run. */\n")
