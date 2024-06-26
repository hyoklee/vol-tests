/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "vol_test_parallel.h"
#include "vol_test_util.h"

#include "vol_file_test_parallel.h"
#include "vol_group_test_parallel.h"
#include "vol_dataset_test_parallel.h"
#include "vol_datatype_test_parallel.h"
#include "vol_attribute_test_parallel.h"
#include "vol_link_test_parallel.h"
#include "vol_object_test_parallel.h"
#include "vol_misc_test_parallel.h"
#ifdef H5VL_TEST_HAS_ASYNC
#include "vol_async_test_parallel.h"
#endif

char vol_test_parallel_filename[VOL_TEST_FILENAME_MAX_LENGTH];

const char *test_path_prefix;

size_t n_tests_run_g;
size_t n_tests_passed_g;
size_t n_tests_failed_g;
size_t n_tests_skipped_g;

int      mpi_size, mpi_rank;
uint64_t vol_cap_flags_g;

/* X-macro to define the following for each test:
 * - enum type
 * - name
 * - test function
 * - enabled by default
 */
#ifdef H5VL_TEST_HAS_ASYNC
#define VOL_PARALLEL_TESTS                                                                                   \
    X(VOL_TEST_NULL, "", NULL, 0)                                                                            \
    X(VOL_TEST_FILE, "file", vol_file_test_parallel, 1)                                                      \
    X(VOL_TEST_GROUP, "group", vol_group_test_parallel, 1)                                                   \
    X(VOL_TEST_DATASET, "dataset", vol_dataset_test_parallel, 1)                                             \
    X(VOL_TEST_DATATYPE, "datatype", vol_datatype_test_parallel, 1)                                          \
    X(VOL_TEST_ATTRIBUTE, "attribute", vol_attribute_test_parallel, 1)                                       \
    X(VOL_TEST_LINK, "link", vol_link_test_parallel, 1)                                                      \
    X(VOL_TEST_OBJECT, "object", vol_object_test_parallel, 1)                                                \
    X(VOL_TEST_MISC, "misc", vol_misc_test_parallel, 1)                                                      \
    X(VOL_TEST_ASYNC, "async", vol_async_test_parallel, 1)                                                   \
    X(VOL_TEST_MAX, "", NULL, 0)
#else
#define VOL_PARALLEL_TESTS                                                                                   \
    X(VOL_TEST_NULL, "", NULL, 0)                                                                            \
    X(VOL_TEST_FILE, "file", vol_file_test_parallel, 1)                                                      \
    X(VOL_TEST_GROUP, "group", vol_group_test_parallel, 1)                                                   \
    X(VOL_TEST_DATASET, "dataset", vol_dataset_test_parallel, 1)                                             \
    X(VOL_TEST_DATATYPE, "datatype", vol_datatype_test_parallel, 1)                                          \
    X(VOL_TEST_ATTRIBUTE, "attribute", vol_attribute_test_parallel, 1)                                       \
    X(VOL_TEST_LINK, "link", vol_link_test_parallel, 1)                                                      \
    X(VOL_TEST_OBJECT, "object", vol_object_test_parallel, 1)                                                \
    X(VOL_TEST_MISC, "misc", vol_misc_test_parallel, 1)                                                      \
    X(VOL_TEST_MAX, "", NULL, 0)
#endif

#define X(a, b, c, d) a,
enum vol_test_type { VOL_PARALLEL_TESTS };
#undef X
#define X(a, b, c, d) b,
static char *const vol_test_name[] = {VOL_PARALLEL_TESTS};
#undef X
#define X(a, b, c, d) c,
static int (*vol_test_func[])(void) = {VOL_PARALLEL_TESTS};
#undef X
#define X(a, b, c, d) d,
static int vol_test_enabled[] = {VOL_PARALLEL_TESTS};
#undef X

static enum vol_test_type
vol_test_name_to_type(const char *test_name)
{
    enum vol_test_type i = 0;

    while (strcmp(vol_test_name[i], test_name) && i != VOL_TEST_MAX)
        i++;

    return ((i == VOL_TEST_MAX) ? VOL_TEST_NULL : i);
}

static void
vol_test_run(void)
{
    enum vol_test_type i;

    for (i = VOL_TEST_FILE; i < VOL_TEST_MAX; i++)
        if (vol_test_enabled[i])
            (void)vol_test_func[i]();
}

hid_t
create_mpi_fapl(MPI_Comm comm, MPI_Info info, hbool_t coll_md_read)
{
    hid_t ret_pl = H5I_INVALID_HID;

    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

    if ((ret_pl = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        goto error;

    if (H5Pset_fapl_mpio(ret_pl, comm, info) < 0)
        goto error;
    if (H5Pset_all_coll_metadata_ops(ret_pl, coll_md_read) < 0)
        goto error;
    if (H5Pset_coll_metadata_write(ret_pl, TRUE) < 0)
        goto error;

    return ret_pl;

error:
    return H5I_INVALID_HID;
} /* end create_mpi_fapl() */

/*
 * Generates random dimensions for a dataspace. The first dimension
 * is always `mpi_size` to allow for convenient subsetting; the rest
 * of the dimensions are randomized.
 */
int
generate_random_parallel_dimensions(int space_rank, hsize_t **dims_out)
{
    hsize_t *dims = NULL;
    size_t   i;

    if (space_rank <= 0)
        goto error;

    if (NULL == (dims = HDmalloc((size_t)space_rank * sizeof(hsize_t))))
        goto error;
    if (MAINPROCESS) {
        for (i = 0; i < (size_t)space_rank; i++) {
            if (i == 0)
                dims[i] = (hsize_t)mpi_size;
            else
                dims[i] = (hsize_t)((rand() % MAX_DIM_SIZE) + 1);
        }
    }

    /*
     * Ensure that the dataset dimensions are uniform across ranks.
     */
    if (MPI_SUCCESS != MPI_Bcast(dims, space_rank, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD))
        goto error;

    *dims_out = dims;

    return 0;

error:
    if (dims)
        HDfree(dims);

    return -1;
}

int
main(int argc, char **argv)
{
    const char *vol_connector_string;
    const char *vol_connector_name;
    unsigned    seed;
    hid_t       fapl_id                   = H5I_INVALID_HID;
    hid_t       default_con_id            = H5I_INVALID_HID;
    hid_t       registered_con_id         = H5I_INVALID_HID;
    char       *vol_connector_string_copy = NULL;
    char       *vol_connector_info        = NULL;
    int         required                  = MPI_THREAD_MULTIPLE;
    int         provided;

    /*
     * Attempt to initialize with MPI_THREAD_MULTIPLE for VOL connectors
     * that require that level of threading support in MPI
     */
    if (MPI_SUCCESS != MPI_Init_thread(&argc, &argv, required, &provided)) {
        HDfprintf(stderr, "MPI_Init_thread failed\n");
        HDexit(EXIT_FAILURE);
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

    if (provided < required) {
        if (MAINPROCESS)
            HDprintf("** INFO: couldn't initialize with MPI_THREAD_MULTIPLE threading support **\n");
    }

    /* Simple argument checking, TODO can improve that later */
    if (argc > 1) {
        enum vol_test_type i = vol_test_name_to_type(argv[1]);
        if (i != VOL_TEST_NULL) {
            /* Run only specific VOL test */
            memset(vol_test_enabled, 0, sizeof(vol_test_enabled));
            vol_test_enabled[i] = 1;
        }
    }

    /*
     * Make sure that HDF5 is initialized on all MPI ranks before proceeding.
     * This is important for certain VOL connectors which may require a
     * collective initialization.
     */
    H5open();

    n_tests_run_g     = 0;
    n_tests_passed_g  = 0;
    n_tests_failed_g  = 0;
    n_tests_skipped_g = 0;

    if (MAINPROCESS) {
        seed = (unsigned)HDtime(NULL);
    }

    if (mpi_size > 1) {
        if (MPI_SUCCESS != MPI_Bcast(&seed, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD)) {
            if (MAINPROCESS)
                HDfprintf(stderr, "Couldn't broadcast test seed\n");
            goto error;
        }
    }

    srand(seed);

    if (NULL == (test_path_prefix = HDgetenv(HDF5_API_TEST_PATH_PREFIX)))
        test_path_prefix = "";

    HDsnprintf(vol_test_parallel_filename, VOL_TEST_FILENAME_MAX_LENGTH, "%s%s", test_path_prefix,
               PARALLEL_TEST_FILE_NAME);

    if (NULL == (vol_connector_string = HDgetenv("HDF5_VOL_CONNECTOR"))) {
        if (MAINPROCESS)
            HDprintf("No VOL connector selected; using native VOL connector\n");
        vol_connector_name = "native";
        vol_connector_info = NULL;
    }
    else {
        char *token = NULL;

        BEGIN_INDEPENDENT_OP(copy_connector_string)
        {
            if (NULL == (vol_connector_string_copy = HDstrdup(vol_connector_string))) {
                if (MAINPROCESS)
                    HDfprintf(stderr, "Unable to copy VOL connector string\n");
                INDEPENDENT_OP_ERROR(copy_connector_string);
            }
        }
        END_INDEPENDENT_OP(copy_connector_string);

        BEGIN_INDEPENDENT_OP(get_connector_name)
        {
            if (NULL == (token = HDstrtok(vol_connector_string_copy, " "))) {
                if (MAINPROCESS)
                    HDfprintf(stderr, "Error while parsing VOL connector string\n");
                INDEPENDENT_OP_ERROR(get_connector_name);
            }
        }
        END_INDEPENDENT_OP(get_connector_name);

        vol_connector_name = token;

        if (NULL != (token = HDstrtok(NULL, " "))) {
            vol_connector_info = token;
        }
    }

    if (MAINPROCESS) {
        HDprintf("Running parallel VOL tests with VOL connector '%s' and info string '%s'\n\n",
                 vol_connector_name, vol_connector_info ? vol_connector_info : "");
        HDprintf("Test parameters:\n");
        HDprintf("  - Test file name: '%s'\n", vol_test_parallel_filename);
        HDprintf("  - Number of MPI ranks: %d\n", mpi_size);
        HDprintf("  - Test seed: %u\n", seed);
        HDprintf("\n\n");
    }

    BEGIN_INDEPENDENT_OP(create_fapl)
    {
        if ((fapl_id = create_mpi_fapl(MPI_COMM_WORLD, MPI_INFO_NULL, FALSE)) < 0) {
            if (MAINPROCESS)
                HDfprintf(stderr, "Unable to create FAPL\n");
            INDEPENDENT_OP_ERROR(create_fapl);
        }
    }
    END_INDEPENDENT_OP(create_fapl);

    BEGIN_INDEPENDENT_OP(check_vol_register)
    {
        /*
         * If using a VOL connector other than the native
         * connector, check whether the VOL connector was
         * successfully registered before running the tests.
         * Otherwise, HDF5 will default to running the tests
         * with the native connector, which could be misleading.
         */
        if (0 != HDstrcmp(vol_connector_name, "native")) {
            htri_t is_registered;

            if ((is_registered = H5VLis_connector_registered_by_name(vol_connector_name)) < 0) {
                if (MAINPROCESS)
                    HDfprintf(stderr, "Unable to determine if VOL connector is registered\n");
                INDEPENDENT_OP_ERROR(check_vol_register);
            }

            if (!is_registered) {
                if (MAINPROCESS)
                    HDfprintf(stderr, "Specified VOL connector '%s' wasn't correctly registered!\n",
                              vol_connector_name);
                INDEPENDENT_OP_ERROR(check_vol_register);
            }
            else {
                /*
                 * If the connector was successfully registered, check that
                 * the connector ID set on the default FAPL matches the ID
                 * for the registered connector before running the tests.
                 */
                if (H5Pget_vol_id(fapl_id, &default_con_id) < 0) {
                    if (MAINPROCESS)
                        HDfprintf(stderr, "Couldn't retrieve ID of VOL connector set on default FAPL\n");
                    INDEPENDENT_OP_ERROR(check_vol_register);
                }

                if ((registered_con_id = H5VLget_connector_id_by_name(vol_connector_name)) < 0) {
                    if (MAINPROCESS)
                        HDfprintf(stderr, "Couldn't retrieve ID of registered VOL connector\n");
                    INDEPENDENT_OP_ERROR(check_vol_register);
                }

                if (default_con_id != registered_con_id) {
                    if (MAINPROCESS)
                        HDfprintf(stderr,
                                  "VOL connector set on default FAPL didn't match specified VOL connector\n");
                    INDEPENDENT_OP_ERROR(check_vol_register);
                }
            }
        }
    }
    END_INDEPENDENT_OP(check_vol_register);

    /* Retrieve the VOL cap flags - work around an HDF5
     * library issue by creating a FAPL
     */
    BEGIN_INDEPENDENT_OP(get_capability_flags)
    {
        vol_cap_flags_g = H5VL_CAP_FLAG_NONE;
        if (H5Pget_vol_cap_flags(fapl_id, &vol_cap_flags_g) < 0) {
            if (MAINPROCESS)
                HDfprintf(stderr, "Unable to retrieve VOL connector capability flags\n");
            INDEPENDENT_OP_ERROR(get_capability_flags);
        }
    }
    END_INDEPENDENT_OP(get_capability_flags);

    /*
     * Create the file that will be used for all of the tests,
     * except for those which test file creation.
     */
    BEGIN_INDEPENDENT_OP(create_test_container)
    {
        if (MAINPROCESS) {
            if (create_test_container(vol_test_parallel_filename, vol_cap_flags_g) < 0) {
                HDfprintf(stderr, "    failed to create testing container file '%s'\n",
                          vol_test_parallel_filename);
                INDEPENDENT_OP_ERROR(create_test_container);
            }
        }
    }
    END_INDEPENDENT_OP(create_test_container);

    /* Run all the tests that are enabled */
    vol_test_run();

    if (MAINPROCESS)
        HDprintf("Cleaning up testing files\n");
    H5Fdelete(vol_test_parallel_filename, fapl_id);

    if (n_tests_run_g > 0) {
        if (MAINPROCESS)
            HDprintf("The below statistics are minimum values due to the possibility of some ranks failing a "
                     "test while others pass:\n");

        if (MPI_SUCCESS != MPI_Allreduce(MPI_IN_PLACE, &n_tests_passed_g, 1, MPI_UNSIGNED_LONG_LONG, MPI_MIN,
                                         MPI_COMM_WORLD)) {
            if (MAINPROCESS)
                HDprintf("    failed to collect consensus about the minimum number of tests that passed -- "
                         "reporting rank 0's (possibly inaccurate) value\n");
        }

        if (MAINPROCESS)
            HDprintf("%s%ld/%ld (%.2f%%) VOL tests passed across all ranks with VOL connector '%s'\n",
                     n_tests_passed_g > 0 ? "At least " : "", (long)n_tests_passed_g, (long)n_tests_run_g,
                     ((float)n_tests_passed_g / (float)n_tests_run_g * 100.0), vol_connector_name);

        if (MPI_SUCCESS != MPI_Allreduce(MPI_IN_PLACE, &n_tests_failed_g, 1, MPI_UNSIGNED_LONG_LONG, MPI_MIN,
                                         MPI_COMM_WORLD)) {
            if (MAINPROCESS)
                HDprintf("    failed to collect consensus about the minimum number of tests that failed -- "
                         "reporting rank 0's (possibly inaccurate) value\n");
        }

        if (MAINPROCESS) {
            HDprintf("%s%ld/%ld (%.2f%%) VOL tests did not pass across all ranks with VOL connector '%s'\n",
                     n_tests_failed_g > 0 ? "At least " : "", (long)n_tests_failed_g, (long)n_tests_run_g,
                     ((float)n_tests_failed_g / (float)n_tests_run_g * 100.0), vol_connector_name);

            HDprintf("%ld/%ld (%.2f%%) VOL tests were skipped with VOL connector '%s'\n",
                     (long)n_tests_skipped_g, (long)n_tests_run_g,
                     ((float)n_tests_skipped_g / (float)n_tests_run_g * 100.0), vol_connector_name);
        }
    }

    if (default_con_id >= 0 && H5VLclose(default_con_id) < 0) {
        if (MAINPROCESS)
            HDfprintf(stderr, "    failed to close VOL connector ID\n");
    }

    if (registered_con_id >= 0 && H5VLclose(registered_con_id) < 0) {
        if (MAINPROCESS)
            HDfprintf(stderr, "    failed to close VOL connector ID\n");
    }

    if (fapl_id >= 0 && H5Pclose(fapl_id) < 0) {
        if (MAINPROCESS)
            HDfprintf(stderr, "    failed to close MPI FAPL\n");
    }

    H5close();

    MPI_Finalize();

    HDexit(EXIT_SUCCESS);

error:
    HDfree(vol_connector_string_copy);

    H5E_BEGIN_TRY
    {
        H5VLclose(default_con_id);
        H5VLclose(registered_con_id);
        H5Pclose(fapl_id);
    }
    H5E_END_TRY;

    MPI_Finalize();

    HDexit(EXIT_FAILURE);
}
