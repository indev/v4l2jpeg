typedef void (*output_callback_func_ptr) ( const void *data, int size );

int internal_main( output_callback_func_ptr callback_ptr );