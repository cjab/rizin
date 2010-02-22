OBJ_PTRACE=io_ptrace.o

STATIC_OBJ+=${OBJ_PTRACE}
TARGET_PTRACE=io_ptrace.${EXT_SO}
ALL_TARGETS+=${TARGET_PTRACE}

${TARGET_PTRACE}: ${OBJ_PTRACE}
	${CC} ${CFLAGS} -o ${TARGET_PTRACE} ${LDFLAGS_LIB} \
		${LDFLAGS_LINKPATH}../util -lr_util \
		${LDFLAGS_LINKPATH}.. -lr_io ${OBJ_PTRACE}
