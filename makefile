all: $(OBJ)

%:
	make -C async_client
	make -C async_server
	make -C async_watcher
