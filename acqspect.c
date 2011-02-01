#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <usb.h>

#define OCEAN_OPTICS  0x2457
#define USB2000PLUS   0x101E
#define USB4000       0x1022

#define EP1Out 0x01
#define EP2In  0x82
#define EP6In  0x86
#define EP1In  0x81

#define USB_CONFIGURATION 1
#define USB_INTERFACE 0
#define USB_ALT_INTERFACE 0

#define TIMEOUT 100

#define BUFFER_SIZE 4096

#ifdef BIGENDIAN
#define COPY_2_BYTES(src,soffset,dest,doffset)  swab(((char*) (src)) + (soffset), ((char*) (dest)) + (doffset), 2)
void swap_32(const void *from, void *to)
{
  char *i, *j;
  for (i = (char*) to, j = (char*) from + 3; i < (to + 4); i++, j--)
    *j = *i;
}
#define COPY_4_BYTES(src,soffset,dest,doffset)  swap_32(((char*) (src)) + (soffset), ((char*) (dest)) + (doffset))
#else
#define COPY_2_BYTES(src,soffset,dest,doffset)  memcpy(((char*) (dest)) + (doffset), ((char*) (src)) + (soffset), 2)
#define COPY_4_BYTES(src,soffset,dest,doffset)  memcpy(((char*) (dest)) + (doffset), ((char*) (src)) + (soffset), 4)
#endif


static struct usb_dev_handle *handle;


void close_spectrometer()
{
  usb_close(handle);
  handle = NULL;
}

void release_spectrometer_interface()
{
  usb_release_interface(handle, USB_INTERFACE);
}

int set_spectrometer_power_mode(char mode)
{
  char cmd[3];

  cmd[0] = 0x04;
  cmd[1] = mode;
  cmd[2] = mode;
  
  if (usb_bulk_write(handle, EP1Out, cmd, 3, TIMEOUT) != 3)
    return 2;

  return 0;
}

void power_down_spectrometer()
{
  /* Clear the endpoint in case something went wrong earlier */
  usb_clear_halt(handle, EP1Out);

  set_spectrometer_power_mode(0);
}

int initialize_spectrometer()
{
  char cmd;

  /* Set up USB communication */
  if (usb_set_configuration(handle, USB_CONFIGURATION) != 0) {
    fprintf(stderr, "ERROR: Set configuration failed.\n");
    return 1;
  }

  if (usb_claim_interface(handle, USB_INTERFACE) != 0) {
    fprintf(stderr, "ERROR: Claim interface failed.\n");
    return 1;
  }

  atexit(release_spectrometer_interface);

  if (usb_set_altinterface(handle, USB_ALT_INTERFACE) != 0) {
    fprintf(stderr, "ERROR: Set altinterface failed.\n");
    return 1;
  }

  /* Clear halt status */
  if (usb_clear_halt(handle, EP1Out) != 0) {
    fprintf(stderr, "ERROR: Clear halt status on endpoint 1 out failed.\n");
    return 1;
  }

  if (usb_clear_halt(handle, EP1In) != 0) {
    fprintf(stderr, "ERROR: Clear halt status on endpoint 1 in failed.\n");
    return 1;
  }

  if (usb_clear_halt(handle, EP2In) != 0) {
    fprintf(stderr, "ERROR: Clear halt status on endpoint 2 in failed.\n");
    return 1;
  }

  /* Initialise spectrometer */
  cmd = 0x01;
  if (usb_bulk_write(handle, EP1Out, &cmd, 1, TIMEOUT) != 1) {
    fprintf(stderr, "ERROR: Initialise spectrometer failed.\n");
    return 1;
  }

  return 0;
}

int set_spectrometer_scaling(double *scaling)
{
  char buffer[17];
  uint16_t value;
  
  /* Acquire saturation level */
  buffer[0] = 0x05;
  buffer[1] = 0x11;
  if (usb_bulk_write(handle, EP1Out, buffer, 2, TIMEOUT) != 2) {
    fprintf(stderr, "ERROR: Acquire saturation level failed.\n");
    return 3;
  }

  if (usb_bulk_read(handle, EP1In, buffer, 17, TIMEOUT) != 17) {
    fprintf(stderr, "ERROR: Read saturation level failed.\n");
    return 3;
  }

  COPY_2_BYTES(buffer, 6, &value, 0);
  *scaling = 65535.0 / value;

  return 0;
}

double get_spectrometer_wavelength(int pixel_number)
{
  static double c[4] = {NAN, NAN, NAN, NAN};
  const double x = (double) pixel_number;
  double value = 0.0;
  int order;

  if (isnan(c[0])) {
    char buffer[17];
    
    for (order = 0; order < 4; order++) {
      buffer[0] = 0x05;
      buffer[1] = (char) order + 1;
      if (usb_bulk_write(handle, EP1Out, buffer, 2, TIMEOUT) != 2) {
	fprintf(stderr, "ERROR: Acquire wavelength calibration failed.\n");
	return 3;
      }
      
      if (usb_bulk_read(handle, EP1In, buffer, 17, TIMEOUT) != 17) {
	fprintf(stderr, "ERROR: Read wavelength calibration failed.\n");
	return 3;
      }
      c[order] = strtod(buffer + 2, NULL);
    }
  }

  for (order = 3; order > 0; order--)
    value = x * (c[order] + value);

  return c[0] + value;
}

double spectrometer_correct_intensity(double intensity)
{
  static double c[8] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};
  static int n = -1;
  double value = 0.0;
  int order;

  if (n == -1) {
    char buffer[17];
    buffer[0] = 0x05;
    buffer[1] = 14;
    
    if (usb_bulk_write(handle, EP1Out, buffer, 2, TIMEOUT) != 2) {
      fprintf(stderr, "ERROR: Acquire polynomial correction order failed.\n");
      return 3;
    }
    
    if (usb_bulk_read(handle, EP1In, buffer, 17, TIMEOUT) != 17) {
      fprintf(stderr, "ERROR: Read polynomial correction order failed.\n");
      return 3;
    }

    n = atoi(buffer + 2);

    for (order = 0; order <= n; order++) {
      buffer[0] = 0x05;
      buffer[1] = (char) order + 6;
      if (usb_bulk_write(handle, EP1Out, buffer, 2, TIMEOUT) != 2) {
	fprintf(stderr, "ERROR: Acquire wavelength calibration failed.\n");
	return 3;
      }
      
      if (usb_bulk_read(handle, EP1In, buffer, 17, TIMEOUT) != 17) {
	fprintf(stderr, "ERROR: Read wavelength calibration failed.\n");
	return 3;
      }
      c[order] = strtod(buffer + 2, NULL);
    }
  }

  for (order = n; order > 0; order--)
    value = intensity * (c[order] + value);
    
  return intensity / (c[0] + value);
}

int set_spectrometer_integration_time(uint32_t integration_time)
{
  char buffer[5];
  buffer[0] = 0x02;

  COPY_4_BYTES(&integration_time, 0, buffer, 1);

  if (usb_bulk_write(handle, EP1Out, buffer, 5, TIMEOUT) != 5) {
    fprintf(stderr, "ERROR: Set integration time failed.\n");
    return 4;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  struct usb_bus *bus;
  struct usb_device *device;
  char buffer[BUFFER_SIZE];
  int i, result;

  uint16_t number_of_pixels, pixel_value;
  uint32_t integration_time = 100000;
  uint32_t spectrum, number_of_spectra = 1;
  double scaling;

  /* This is a global in order to use the atexit facility */
  handle = NULL;

  /* Get command line options */
  for(i = 1; i < argc && argv[i][0] == '-' &&
	argv[i][1] != '\0' && argv[i][2] == '\0'; i++) {
    if(!strcmp(argv[i], "--")) {
      i++;
      break;
    }
    switch(argv[i][1]) {
    case 't':
      if(++i < argc)
	integration_time = strtoul(argv[i], NULL, 10);
      else {
	fprintf(stderr, "Missing argument to option -- t\n");
	return 1;
      }
      break;
    case 'n':
      if(++i < argc)
	number_of_spectra = strtoul(argv[i], NULL, 10);
      else {
	fprintf(stderr, "Missing argument to option -- n\n");
	return 1;
      }
      break;
    case 'v':
      fprintf(stderr, "acqspect-"VERSION", © 2011, Sumant S.R. Oemrawsingh, see LICENSE for details\n");
      return 0;
      break;
    case 'h':
      fprintf(stderr, "Usage: %s [options]\n", argv[0]);
      fprintf(stderr, "Acquire a spectrum from an Ocean Optics USB2000+ spectrometer.\n\n");
      fprintf(stderr, "Options:\n");
      fprintf(stderr, "-t NUM  Set integration time in microseconds (default: 100000).\n");
      fprintf(stderr, "-n NUM  Number of acquisitions (default: 1).\n");
      fprintf(stderr, "-v      Print version information and exit.\n");
      fprintf(stderr, "-h      Print this help message and exit.\n");
      return 0;
      break;
    default:
      fprintf(stderr, "Error parsing argument -- %c\n", argv[i][1]);
      return 1;
    }
  }

  usb_init();
  usb_find_busses();
  usb_find_devices();

  for (bus = usb_get_busses(); !handle && bus; bus = bus->next)
    for (device = bus->devices; !handle && device; device = device->next)
      if (device->descriptor.idVendor == OCEAN_OPTICS &&
	  device->descriptor.idProduct == USB2000PLUS) {
	handle = usb_open(device);
	atexit(close_spectrometer);
	break;
      }

  if (!handle) {
    fprintf(stderr, "ERROR: No Ocean Optics devices found.\n");
    return 1;
  }

  if ((result = initialize_spectrometer()) != 0)
    return 1;

  /* Set trigger mode */
  buffer[0] = 0x0a;
  buffer[1] = 0x00;
  buffer[2] = 0x00;
  if ((result = usb_bulk_write(handle, EP1Out, buffer, 3, TIMEOUT)) != 3) {
    fprintf(stderr, "ERROR: Set trigger mode failed.\n");
    return result;
  }

  if (set_spectrometer_integration_time(integration_time) != 0)
    return 4;

  /* Acquire status. For some reason, the time-out after increasing
     the integration time must be increased as well, for the first
     command after setting the integration time. At least, this seems
     to work, whereas just setting the time-out to the default
     TIMEOUT, may cause read errors (146). */
  buffer[0] = -2; /* 0xfe */
  if ((result = usb_bulk_write(handle, EP1Out, buffer, 1, TIMEOUT + integration_time)) != 1) {
    fprintf(stderr, "ERROR: Acquire status failed.\n");
    return result;
  }

  if ((result = usb_bulk_read(handle, EP1In, buffer, 16, TIMEOUT)) != 16) {
    fprintf(stderr, "ERROR: Read status failed.\n");
    return result;
  }

  COPY_2_BYTES(buffer, 0, &number_of_pixels, 0);
 
  /* Set scaling via saturation level */
  if (set_spectrometer_scaling(&scaling) != 0)
    return 2;

  /* Power on spectrometer */
  if (set_spectrometer_power_mode(1) != 0) {
    fprintf(stderr, "ERROR: Power on failed.\n");
    return 3;
  }
  atexit(power_down_spectrometer);

  /* Acquire and print spectra */
  for (spectrum = 0; spectrum < number_of_spectra; spectrum++) {
    /* Acquire spectrum */
    do {
      buffer[0] = 0x09;
      if ((result = usb_bulk_write(handle, EP1Out, buffer, 1, TIMEOUT)) != 1) {
	fprintf(stderr, "ERROR: Requesting spectrum failed.\n");
	return result;
      }
      /* XXX For USB4000, data comes in on EP6In as well, when in high speed mode */
      result = usb_bulk_read(handle, EP2In, buffer, 2 * number_of_pixels + 1,
			     TIMEOUT + integration_time/1000);
    } while (result < 2 * number_of_pixels + 1 ||
	     buffer[2 * number_of_pixels] != 0x69);
    
    /* Print spectrum */
    for (i = 0; i < number_of_pixels; i++) {
      COPY_2_BYTES(buffer, 2*i, &pixel_value, 0);
      printf("%f %f\n",
	     get_spectrometer_wavelength(i),
	     pixel_value * scaling);
	     /*	     spectrometer_correct_intensity(pixel_value * scaling));*/
    }
    printf("\n\n");
  }

  /* End of line: let atexit-registered functions handle it all. */
  
  return 0;
}
