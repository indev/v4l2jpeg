#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <time.h>
#include <stdbool.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include "v4l2jpeg.h"
#include "mjpegtojpeg.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

enum io_method {
  IO_METHOD_READ,
  IO_METHOD_MMAP,
  IO_METHOD_USERPTR
};

enum output_method {
  OUTPUT_STDOUT,
  OUTPUT_FILE,
  OUTPUT_CALLBACK
};

struct buffer {
  void *data;
  size_t length;
};

struct buffer *buffers = NULL;
static int fd = -1;
static unsigned int num_buffers;
static enum io_method io = IO_METHOD_MMAP;
static enum output_method output = OUTPUT_FILE;

/** settings **/
static char *device_name;
static unsigned int v_width = 160;
static unsigned int v_height = 120;
static int num_frames = -1;
static int framerate = 2;

unsigned int current_file_index = 0;
static char *output_file_pattern;

bool use_jpeg_header_boundry = false;

output_callback_func_ptr output_callback_ptr = NULL;


void errno_exit(const char *err)
{
  fprintf(stderr, "%s error %d, %s\n", err, errno, strerror(errno));
  exit(EXIT_FAILURE);
}

int xioctl(int fh, int request, void *arg)
{
  int r;

  do {
    r = ioctl(fh, request, arg);
  } while (  r == -1 && EINTR == errno  );

  return r;
}

void device_open()
{
  struct stat st;

  fprintf(stderr, "Try to open device %s\n", device_name);

  // get the info 
  if ( stat(device_name, &st) == -1  ) {
    fprintf(stderr, "Cannot identify device: %s, %d, %s\n", device_name, errno, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // make sure it's a deivce
  if ( !S_ISCHR(st.st_mode) ) {
    fprintf(stderr, "%s is no device\n", device_name);
    exit(EXIT_FAILURE);
  }

  // open the device
  fd = open(device_name, O_RDWR | O_NONBLOCK, 0);

  if ( fd == -1 ) {
     fprintf(stderr, "Cannot open device: %s, %d, %s\n", device_name, errno, strerror(errno));
     exit(EXIT_FAILURE);
  }

  fprintf(stderr, "Device open\n");
}

void device_close()
{
  if ( close(fd)  ) {
    fprintf(stderr, "Cannot close device\n");
    exit(EXIT_FAILURE);
  }

  fd = -1;
  fprintf(stderr, "Device closed\n");
}

void io_read_init(unsigned int buffer_size)
{
  buffers = (struct buffer*)calloc(1, sizeof(*buffers));

  if ( !buffers ) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }

  buffers[0].length = buffer_size;
  buffers[0].data = malloc(buffer_size);

  if ( !buffers[0].data ) {
    fprintf(stderr,"Out of memory\n");
    exit(EXIT_FAILURE);
  }

}

void io_mmap_init()
{
  struct v4l2_requestbuffers req;

  CLEAR(req);

  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if ( xioctl(fd, VIDIOC_REQBUFS, &req) == -1 ) {
    if ( errno == EINVAL ) {
      fprintf(stderr, "%s does not support memory mapping\n", device_name);
      exit(EXIT_FAILURE);
    }
    else {
      errno_exit("VIDIOC_REQBUFS");
    }
  }

  if ( req.count < 2 ) {
    fprintf(stderr, "Insufficient buffer memory on %s\n", device_name);
    exit(EXIT_FAILURE);
  }

  buffers = (struct buffer*)calloc(req.count, sizeof(*buffers));

  if ( !buffers ) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }

  for ( num_buffers = 0; num_buffers < req.count; ++num_buffers )
  {
    struct v4l2_buffer buf;

    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = num_buffers;

    if ( xioctl(fd, VIDIOC_QUERYBUF, &buf) ) 
      errno_exit("VIDIOC_QUERYBUF");

    buffers[num_buffers].length = buf.length;
    buffers[num_buffers].data = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

    if ( buffers[num_buffers].data == MAP_FAILED )
      errno_exit("mmap");
  }

  fprintf(stderr, "Mapped %d buffers\n", num_buffers);
}

void init_device()
{
  struct v4l2_capability cap;
  struct v4l2_format fmt;

  if ( xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1 ) {
    if ( errno == EINVAL ) {
      fprintf(stderr, "%s in no valid V4L2 device\n", device_name);
      exit(EXIT_FAILURE);
    }
    else
      errno_exit("VIDIOC_QUERYCAP");
  }

  if ( !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ) {
    fprintf(stderr, "%s is no video capture device\n", device_name);
    exit(EXIT_FAILURE);
  }

  switch(io) {
  case IO_METHOD_READ:
    if ( !(cap.capabilities & V4L2_CAP_READWRITE) ) {
      fprintf(stderr, "%s does not support read i/o\n", device_name);
      exit(EXIT_FAILURE);
    }
    break;

  case IO_METHOD_MMAP:
  case IO_METHOD_USERPTR:
    if ( !(cap.capabilities & V4L2_CAP_STREAMING) ) {
      fprintf(stderr, "%s does not support streaming\n", device_name);
      exit(EXIT_FAILURE);
    }
    break;
  }

  CLEAR(fmt);
  
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if ( xioctl(fd, VIDIOC_G_FMT, &fmt) == -1 )
    errno_exit("VIDIOC_G_FMT");

  if ( fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG ) {
    fprintf(stderr, "Not using MJPEG as data format\n");
  }

  // try to change the format
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = v_width;
  fmt.fmt.pix.height = v_height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
  
  if ( xioctl(fd, VIDIOC_S_FMT, &fmt) == -1 ) 
    errno_exit("VIDIOC_S_FMT");

  if ( fmt.fmt.pix.width != v_width ) {
    v_width = fmt.fmt.pix.width;
    fprintf(stderr, "Device reset width to %d\n", v_width);
  }

  if ( fmt.fmt.pix.height != v_height ) {
    v_height = fmt.fmt.pix.height;
    fprintf(stderr, "Device reset height to %d\n", v_height);
  }


  if ( fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG ) {
    errno_exit("VIDIOC_S_FMT: Unable to set V4L2_PIX_FMT_MJPEG");
  }
  
  switch(io) {
  case IO_METHOD_READ:
    io_read_init(fmt.fmt.pix.sizeimage);
    break;
  case IO_METHOD_MMAP:
    io_mmap_init();
    break;
  case IO_METHOD_USERPTR:
    break;
  }
}

void uninit_device()
{
  unsigned int i;

  switch(io)
  {
  case IO_METHOD_READ:
    free(buffers[0].data);
    break;

  case IO_METHOD_MMAP:
    for (i=0; i<num_buffers; ++i) {
      if ( munmap(buffers[i].data, buffers[i].length) == -1 )
        errno_exit("munmap");
    }
    break;

  case IO_METHOD_USERPTR:
    break;
  }
}

void start_capturing()
{
  unsigned int i;
  enum v4l2_buf_type type;
  

  switch(io)
  {
  case IO_METHOD_READ:
    break;
  case IO_METHOD_MMAP:
    for (i=0; i < num_buffers; ++i) {
      struct v4l2_buffer buf;
      CLEAR(buf);
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;

      if ( xioctl(fd,VIDIOC_QBUF,&buf) == -1 ) {
        errno_exit("VIDIOC_QBUF");
      }
    }
    
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if ( xioctl(fd,VIDIOC_STREAMON,&type) == -1 )
      errno_exit("VIDIOC_STREAMON");

    break;
  case IO_METHOD_USERPTR:
    break;
  }
}

void stop_capturing()
{
  enum v4l2_buf_type type;

  switch(io) {
  case IO_METHOD_READ:
    break;
  case IO_METHOD_USERPTR:
  case IO_METHOD_MMAP:
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if ( xioctl(fd,VIDIOC_STREAMOFF, &type) == -1 )
      errno_exit("VIDIOC_STREAMOFF");
    break;
  }
}

void output_data_stdout(const void *data, int size)
{
  FILE *fp = stdout;

  char jpegHeader[256];
  char jpegBoundry[] = "\n--v4l2jpegboundry\n";

  snprintf( jpegHeader, sizeof(char) * 256, "Content-Type: image/jpeg\n Content-Length: %d\n\n", size );
  
  if (use_jpeg_header_boundry)
    fwrite(jpegHeader, sizeof(char), strlen(jpegHeader), fp);

  fwrite(data, size, 1, fp);

  if (use_jpeg_header_boundry)
    fwrite(jpegBoundry, sizeof(char), strlen(jpegBoundry), fp);

}

void output_data_file(const void *data, int size)
{
  char *filename;
  FILE *fp = NULL;

  filename = (char *)malloc( sizeof(char) * 1024 );

  snprintf(filename, 1024, output_file_pattern, current_file_index++);

  fp = fopen(filename, "wb");
  fwrite( data, size, 1, fp);
  fclose(fp);

  free(filename);
}

void output_callback(const void *data, int size)
{
  if ( output_callback_ptr != NULL ) {
    (*output_callback_ptr)(data, size);
  }
}

void output_data(const void *data, int size)
{
 
  switch( output )
  {
    case OUTPUT_STDOUT:
      output_data_stdout(data, size);
    break;

    case OUTPUT_FILE:
      output_data_file(data, size);
    break;

    case OUTPUT_CALLBACK:
      output_callback(data, size);
    break;
  }
}

void process_image(const void *p, int size) {
  byte *jpg = NULL;
  unsigned int jpgSize = 0;

  jpg = mjpeg2jpeg((const byte*)p, size, &jpgSize);

  if ( jpg == NULL )
    errno_exit("mjpeg2jpeg");

  fprintf(stderr, "process_image %d\n", jpgSize);
  output_data(jpg, jpgSize);

  free(jpg);
}

int read_frame()
{
  struct v4l2_buffer buf;

  switch(io) {
  case IO_METHOD_READ:
    if ( read(fd, buffers[0].data, buffers[0].length) == -1 ) {
      switch(errno) {
      case EAGAIN:
        return 0;
      case EIO:
      default:
        errno_exit("read");
      }
    }

    process_image(buffers[0].data, buffers[0].length);
    break;

  case IO_METHOD_MMAP:
    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if ( xioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
      switch(errno) {
      case EAGAIN:
        return 0;
      case EIO:
      default:
        errno_exit("VIDIOC_DQBUF");
      }
    }

    assert( buf.index < num_buffers );

    process_image(buffers[buf.index].data, buf.bytesused);

    if ( xioctl(fd, VIDIOC_QBUF, &buf) == -1)
      errno_exit("VIDIOC_DBUF");

    break;

  case IO_METHOD_USERPTR:
    break;
  }

  return 1;
}

void capture_frame_loop()
{

  for (;;) {
    fd_set fds;
    struct timeval tv;
    int r;

    FD_ZERO(&fds);
    FD_SET(fd,&fds);

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    r = select( fd+1, &fds, NULL, NULL, &tv );

    if ( r == -1 ) {
      if (errno == EINTR)
        continue;

      errno_exit("select");
    }

    if ( r == 0 ) {
      fprintf(stderr,"select timeout\n");
      //exit(EXIT_FAILURE);
      continue;
    }

    if ( read_frame() )
      break;
  }
}

void fps_delay() 
{
  float ms_delay = (1.0f / framerate) * 1000000 ;
  
  usleep(ms_delay);
}

void capture_loop()
{
  int counter;

  counter = num_frames;

  if ( counter == -1  ) {
    // capture frams indefinetly
    while( 1 ) {
      fps_delay();

      capture_frame_loop();
    }
  }
  else {
    // only capture a specific number of frames
    while ( counter-- > 0 ) {
      fps_delay();

      capture_frame_loop();
    
    }
  }
}

void init_defaults()
{
  device_name = (char*)malloc( strlen("/dev/video0") * sizeof(char) ) ;
  strcpy( device_name, "/dev/video0");


  output_file_pattern = (char*)malloc( strlen("files/dump_%d.jpg") * sizeof(char) ) ;
  strcpy( output_file_pattern, "files/dump_%d.jpg");
 
}

#ifdef OUTPUT_INTERNAL

int internal_main( output_callback_func_ptr callback_ptr )
{
  output = OUTPUT_CALLBACK;

  output_callback_ptr = callback_ptr;

  device_open();
  init_device();
  start_capturing();
 
  capture_loop();
 
  stop_capturing();
  uninit_device();
  device_close();

  return 0;
}

#else

static void usage(FILE *fp, int argc, char **argv)
{
  fprintf(fp, 
    "Usage: %s [options]\n\n"
    "Options:\n"
    "-d | --device name\n"
    "-h | --help\n"
    "-W | --width px\n"
    "-H | --height px\n"
    "-c | --count x (num frames to capture, ignore for infinite)\n"
    "-f | --fps x\n"
    "-o | --output to stdout\n"
    "-j | --insert a jpeg header and boundry when outputting to stdout\n"
    "",
    argv[0]);
}

static const char short_options [] = "d:h:W:H:c:f:j";

static const struct option long_options [] = {
  { "device", required_argument, NULL, 'd' },
  { "help", no_argument, NULL, 'h' },
  { "width", required_argument, NULL, 'W' },
  { "height", required_argument, NULL, 'H' },
  { "count", required_argument, NULL, 'c' },
  { "fps", required_argument, NULL, 'f' },
  { "jpeg", no_argument, NULL, 'j' },
  { "stdout", no_argument, NULL, 'o' },
  { 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
  init_defaults();

  while (1) {
    int index, c = 0;

    c = getopt_long(argc, argv, short_options, long_options, &index);

    if (c == -1)
      break;

    switch (c) {
    case 0:
      break;

    case 'd':
      free(device_name);
      device_name = optarg;
      break;

    case 'W':
      v_width = atoi(optarg);
      break;

    case 'H':
      v_height = atoi(optarg);
      break;

    case 'c':
      num_frames = atoi(optarg);
      break;

    case 'f':
      framerate = atoi(optarg);
      break;

    case 'j':
      use_jpeg_header_boundry = true;
    break;

    case 'o':
      output = OUTPUT_STDOUT;
    break;

    default:
      usage(stderr, argc, argv);
      exit(EXIT_FAILURE);
    }

  }

  if ( io == IO_METHOD_USERPTR ) {
    fprintf(stderr, "IO_METHOD_USERPTR not supported yet, using MMAP instead");
    io = IO_METHOD_MMAP;
  }


  device_open();
  init_device();
  start_capturing();
 
  capture_loop();
 
  stop_capturing();
  uninit_device();
  device_close();

  return 0;
}
#endif
