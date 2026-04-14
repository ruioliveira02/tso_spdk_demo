#include <spdk/bdev.h>
#include <spdk/bdev_module.h>
#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/log.h>
#include <spdk/string.h>
#include <stdint.h>

#define UNUSED(x) (void)x

/*
 * Application configuration options
 */
struct entropy_config {
    const char* bdev_name; /* Name of the block device to use */
    uint32_t max_queue_depth; /* Maximum number of outstanding I/O requests in any given I/O channel */
    uint32_t thread_count; /* How many threads we will use */
};

/*
 * This variable stores application-specific configuration options provided by users. As we cannot pass
 * context to spdk_app_parse_args, it must be a global variable
 */
static struct entropy_config g_config;

/*
 * This struct stores application-specific context, including the block we are currently reading and the
 * frequency of each byte value so far
 */
struct entropy_job {
    struct spdk_bdev* bdev; /* A struct describing the bdev we are using */
    struct spdk_bdev_desc* bdev_desc; /* A descriptor for the bdev */
    struct spdk_io_channel* io_channel; /* The I/O channel we use for reading. Each SPDK thread must use
                                           its own I/O channel, obtained by calling spdk_bdev_get_io_channel*/
    struct spdk_bdev_io_wait_entry bdev_io_wait; /* bdev I/O submission can fail with rc=-ENOMEM
                                                    if the spdk_bdev_io buffer could not be allocated. In such
                                                    cases, we must wait until a buffer is available, by calling
                                                    spdk_bdev_queue_io_wait with this struct properly filled out */

    const char* bdev_name; /* The name of the block device we are using*/
    uint64_t block_count; /* The number of blocks in the bdev */
    uint64_t current_block; /* The offset of the block we are currently reading from */
    uint32_t io_size; /* The size of each I/O in bytes */
    unsigned char* buff; /* A DMA memory region that we will use as a buffer for I/O operations */

    uint64_t frequencies[256]; /* An array tracking how often each different byte-value has been seen */
};

/*
 * An auxiliary function that parses the value of a given flag, and updates
 * g_config accordingly
 */
static int parse_args(int flag, char* value) {
    long long tmp = 0;
    if (flag == 'q' || flag == 't') {
        tmp = spdk_strtoll(value, 10);
        if (tmp < 0) {
            fprintf(stderr, "Parse failed for the option %c.\n", flag);
            return tmp;
        } else if (tmp >= INT_MAX) {
            fprintf(stderr, "Parsed option was too large %c.\n", flag);
            return -ERANGE;
        }
    }

    switch (flag) {
    case 'b':
        g_config.bdev_name = value;
        break;
    case 'q':
        g_config.max_queue_depth = tmp;
        break;
    case 't':
        g_config.thread_count = tmp;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}
/*
 * An auxiliary function that explains how to run our application
 */
static void usage(void) {
    printf(" -b <bdev>                 name of the bdev to use\n");
    printf(" -q <depth>                io depth\n");
    printf(" -t <thread counts>        number of threads to use\n");
}

static void free_job(struct entropy_job* job) {
    if (job->buff)
        spdk_dma_free(job->buff);
    free(job);
}

static struct entropy_job* create_job(void) {
    struct entropy_job* job = calloc(1, sizeof(struct entropy_job));
    if (!job)
        return NULL;

    job->bdev_name = g_config.bdev_name;

    return job;
}

/* Terminate the application */
static void entropy_end(struct entropy_job* job, int rc) {
    spdk_put_io_channel(job->io_channel); // Close the I/O channel
    spdk_bdev_close(job->bdev_desc); // Close the bdev descriptor
    free_job(job);
    spdk_app_stop(rc); // Stop the SPDK application framework
}

/*
 * A function that handles bdev removal. If our underlying bdev is removed while
 * we are working, we must stop using it. In this case, it means our application
 * must stop.
 */
static void bdev_removed(enum spdk_bdev_event_type type, struct spdk_bdev* bdev,
                         void* event_ctx) {
    UNUSED(bdev);
    struct entropy_job* job = event_ctx;
    if (type == SPDK_BDEV_EVENT_REMOVE) {
        SPDK_ERRLOG("Bdev %s has been removed. Aborting.\n", job->bdev_name);
        entropy_end(job, -1);
    }
}

/*
 * An auxiliary function that opens a descriptor to the target bdev and creates
 * an I/O channel for it
 */
static int open_bdev(struct entropy_job* job) {
    // Open bdev in read-only mode
    int rc = spdk_bdev_open_ext(job->bdev_name, 0, bdev_removed, job,
                                &job->bdev_desc);

    if (rc == 0) {
        // Get a descriptor to the bdev and get important information
        job->bdev = spdk_bdev_desc_get_bdev(job->bdev_desc);
        job->block_count = job->bdev->blockcnt;
        job->io_size = job->bdev->blocklen;

        // Allocate DMA memory to use as I/O buffer
        job->buff = spdk_dma_malloc(job->io_size, 0, NULL);
        if (!job->buff) {
            SPDK_ERRLOG("Unable to allocate DMA buffer.\n");
            spdk_bdev_close(job->bdev_desc);
        }

        // Open an I/O channel associated with the bdev descriptor
        job->io_channel = spdk_bdev_get_io_channel(job->bdev_desc);
        if (!job->io_channel) {
            SPDK_ERRLOG("Unable to open I/O channel to bdev.\n");
            spdk_bdev_close(job->bdev_desc);
        }
    } else {
        SPDK_ERRLOG("Unable to open bdev for reading: %d.\n", rc);
    }

    return rc;
}

static float compute_entropy(uint64_t frequencies[], int N,
                             uint64_t byte_count) {
    float entropy = 0.0f;
    for (int i = 0; i < N; i++) {
        if (frequencies[i] != 0) {
            float p = (float)frequencies[i] / byte_count;
            entropy -= p * log2(p);
        }
    }

    return entropy;
}

/* Called when we finish processing the last block */
static void entropy_complete(struct entropy_job* job) {
    float entropy =
        compute_entropy(job->frequencies, 256, job->block_count * job->io_size);

    SPDK_NOTICELOG("Final entropy: %f bits.\n", entropy);
    entropy_end(job, 0);
}

/* "Main" application function. This is where we will submit our I/O requests */
static void entropy_run(void* ctx) {
        // Write your code here
       entropy_complete(ctx); 
}

/* SPDK application entry-point */
static void entropy_start(void* ctx) {
    UNUSED(ctx);

    struct entropy_job* job = create_job();
    if (!job) {
        SPDK_ERRLOG("Job creation failed.\n");
        spdk_app_stop(-1);
        return;
    }

    SPDK_NOTICELOG("App started\n");
    int rc = open_bdev(job);
    if (rc) {
        free_job(job);
        SPDK_ERRLOG("Unable to open bdev.\n");
        spdk_app_stop(-1);
        return;
    }

    entropy_run(job);
}

static void initialize_global_config(void) {
    g_config.bdev_name = "";
    g_config.max_queue_depth = 1;
    g_config.thread_count = 1;
}

int main(int argc, char** argv) {
    // Initialize SPDK arguments
    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts, sizeof(opts));
    opts.name = "TSO SPDK Example";

    initialize_global_config();

    int rc = spdk_app_parse_args(argc, argv, &opts, "b:q:t:", NULL, parse_args,
                                 usage);
    if (rc == SPDK_APP_PARSE_ARGS_FAIL) {
        SPDK_ERRLOG("Invalid arguments.\n");
        return rc;
    } else if (rc == SPDK_APP_PARSE_ARGS_HELP) {
        return 0;
    }

    // This will call entropy_start and will block until
    // spdk_app_stop is called
    rc = spdk_app_start(&opts, entropy_start, NULL);

    spdk_app_fini(); // Cleanup SPDK framework

    return 0;
}
