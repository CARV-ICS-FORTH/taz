// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#include <mpi.h>

/**
 * @brief
 *
 * clang-format off
 *
 ****** ignored *******
 * profiler=skip
 * replay=not relevant
 * void
 * void *,const void *                                  (addresses in onesided)
 * MPI_Offset, MPI_Offset *                             (addresses in files)
 * MPI_Aint, MPI_Aint *, MPI_Aint [], const MPI_Aint [] (absolute addresses)
 *
 ****** integers ******
 * profiler=save as is
 * replay=set as is
 * int, int *, int [], const int []
 * MPI_Count , MPI_Count *
 * MPI_Fint, MPI_Fint *, const MPI_Fint *
 *
 *****Function pointers*****
 * profiler= symbol name > relative offset to text start > absolute offset
 * replay=not relevant
 * MPI_Comm_copy_attr_function *, MPI_Comm_delete_attr_function *,
 * MPI_Comm_errhandler_function *, MPI_Datarep_conversion_function *,
 * MPI_Datarep_extent_function *
 * MPI_File_errhandler_function *
 * MPI_Grequest_cancel_function *, MPI_Grequest_free_function *,
 * MPI_Grequest_query_function *
 * MPI_Type_copy_attr_function *,
 * MPI_Type_delete_attr_function *, MPI_User_function *
 * MPI_Win_copy_attr_function *, MPI_Win_delete_attr_function *,
 * MPI_Win_errhandler_function *
 *
 *****Data type*****
 * profiler=use to compute sizes in bytes
 * replay=not relevant
 * MPI_Datatype, MPI_Datatype *, MPI_Datatype [], const MPI_Datatype []
 *
 ***** Communicators / Groups ******
 * profiler=save participating ranks
 * replay= recreate corresponding communicators
 * MPI_Comm, MPI_Comm *
 * MPI_Group, MPI_Group *
 *
 ***** Opaque types ******
 * profiler=map to a variable on creation, and track utilizations
 * replay=not relevant
 * MPI_Errhandler, MPI_Errhandler *
 * MPI_File, MPI_File *
 * MPI_Info, MPI_Info *, const MPI_Info []
 * MPI_Op, MPI_Op *
 * MPI_Win, MPI_Win *
 *
 ***** Communication state *****
 * profiler=save content of the classes on creation
 * replay=not relevant
 * MPI_Message, MPI_Message *
 * MPI_Request, MPI_Request *, MPI_Request []
 * MPI_Status *, MPI_Status [], const MPI_Status *
 *
 ****** strings ******
 * profiler= save as is
 * replay= not relevant
 * char *, char ***, char **[], char *[], const char *, const char []
 *
 *
 * clang-format on
 */

class MpiObjectsStorage {

  template <typename T> uint32_t create_variable(T opaque);
  template <typename T> uint32_t get_variable(T opaque);

  template <typename T, typename O> void set_content(T opaque, O new_value);
  template <typename T, typename O> O get_content(T opaque);

  std::unordered_map<MPI_Comm, std::pair<uint32_t, std::vector<int>>> comms;
  std::unordered_map<MPI_Group, std::pair<uint32_t, std::vector<int>>> comms;
};