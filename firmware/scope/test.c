//----------------------------------------------------------------------------------------------------------------------------------
#include "types.h"
#include "menu.h"
#include "test.h"
#include "scope_functions.h"
#include "statemachine.h"
#include "touchpanel.h"
#include "timer.h"
#include "fpga_control.h"
#include "spi_control.h"
#include "sd_card_interface.h"
#include "display_lib.h"
#include "ff.h"
#include "DS3231.h"

#include "usb_interface.h"
#include "variables.h"

#include "sin_cos_math.h"

//sinx
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

//endsinx

  #include <stdint.h>
#include <stddef.h>  // Pre definíciu size_t

#include <string.h>
//----------------------------------------------------------------------------------------------------------------------------------
/*
  //Check if in waveform view
  if(scopesettings.waveviewmode)
  {
    //Display the file name
    //Use white text and font_0
    display_set_fg_color(WHITE_COLOR);
    display_set_font(&font_0);
    
    if(onoffRTC) //if RTCon view rtc info
      {
      thumbnaildata = &viewthumbnaildata[viewcurrentindex];
      decodethumbnailfilename(thumbnaildata->filename);
      display_text(488, 70, filenameRTC);   //500 48 , 490 58
      }   else display_text(555, 70, viewfilename); //550 48
  }
  */

//----------------------------------------------------------------------------------------------------------------------------------
 
#include "ff.h"
#include "stdint.h"

// --- Constants defining file structure ---
#define SETTINGS_SIZE   0x320   // 800 bytes for settings
#define CH1_START       0x320
#define CH2_START       0xED8
#define CH_SAMPLES      3000

// --- Read 32-bit little-endian value ---
static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

// --- Write text to file (no stdlib) ---
static void write_text(FIL *fp, const char *txt)
{
    UINT bw;
    uint32_t len = 0;
    const char *p = txt;
    while (*p++) len++;
    f_write(fp, txt, len, &bw);
}

// --- Write unsigned integer as ASCII ---
static void write_uint(FIL *fp, uint32_t value)
{
    char buf[12];
    char *p = buf + 11;
    *p = 0;
    if (value == 0)
    {
        *(--p) = '0';
    }
    else
    {
        while (value && p > buf)
        {
            *(--p) = '0' + (value % 10);
            value /= 10;
        }
    }
    write_text(fp, p);
}

/*
 * Convert all waveform files (*.wav) from folder "waveforms"
 * into CSV files stored in folder "cvs_format".
 * No standard C library functions are used.
 */
FRESULT convert_waveforms_to_csv_excel(void)
{
    DIR dir;
    FIL file_in, file_out;
    FILINFO fno;
    FRESULT fr;
    UINT br, bw;
    BYTE header[SETTINGS_SIZE];
    BYTE ch1[CH_SAMPLES];
    BYTE ch2[CH_SAMPLES];

    const char *path_in  = "waveforms";
    const char *path_out = "cvs_format";

    // Create output folder if not present
    f_mkdir(path_out);

    fr = f_opendir(&dir, path_in);
    if (fr != FR_OK)
        return fr;

    // Read all files from "waveforms"
    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0)
            break;
        if (fno.fattrib & AM_DIR)
            continue;

        // Check for ".wav" extension
        char *name = fno.fname;
        int len = 0;
        while (name[len])
            len++;
        if (len < 4)
            continue;
        if (!(name[len - 4] == '.' && name[len - 3] == 'w' &&
              name[len - 2] == 'a' && name[len - 1] == 'v'))
            continue;

        // Build full paths
        char in_path[64];
        char out_path[64];
        int i = 0, j = 0;

        // in_path = "waveforms/" + filename
        while (path_in[i])
        {
            in_path[i] = path_in[i];
            i++;
        }
        in_path[i++] = '/';
        while (name[j] && i < 63)
            in_path[i++] = name[j++];
        in_path[i] = 0;

        // out_path = "cvs_format/" + filename with ".csv"
        i = 0;
        while (path_out[i])
        {
            out_path[i] = path_out[i];
            i++;
        }
        out_path[i++] = '/';
        j = 0;
        while (name[j] && i < 59)
        {
            if (name[j] == '.')
            {
                out_path[i++] = 'c';
                out_path[i++] = 's';
                out_path[i++] = 'v';
                out_path[i] = 0;
                break;
            }
            out_path[i++] = name[j++];
        }

        // --- Read waveform file ---
        fr = f_open(&file_in, in_path, FA_READ);
        if (fr != FR_OK)
            continue;

        f_read(&file_in, header, SETTINGS_SIZE, &br);
        f_lseek(&file_in, CH1_START);
        f_read(&file_in, ch1, CH_SAMPLES, &br);
        f_read(&file_in, ch2, CH_SAMPLES, &br);
        f_close(&file_in);

        // Extract some settings
        uint32_t volt_div_ch1 = read_le32(&header[36]);
        uint32_t volt_div_ch2 = read_le32(&header[132]);
        uint32_t sample_rate  = read_le32(&header[228]);
        uint32_t time_div     = read_le32(&header[224]);
        uint32_t trig_ch      = read_le32(&header[240]);
        uint32_t trig_level   = read_le32(&header[244]);
        uint32_t trig_mode    = read_le32(&header[232]);
        uint32_t trig_edge    = read_le32(&header[236]);

        // --- Create CSV file ---
        fr = f_open(&file_out, out_path, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr != FR_OK)
            continue;

        // UTF-8 BOM
        const BYTE bom[3] = {0xEF, 0xBB, 0xBF};
        f_write(&file_out, bom, 3, &bw);

        // Metadata
        write_text(&file_out, "# FNIRSI 1013D waveform export\n");
        write_text(&file_out, "# File name; ");
        write_text(&file_out, name);
        write_text(&file_out, "\n# Sample rate; ");
        write_uint(&file_out, sample_rate);
        write_text(&file_out, " Hz\n# Time per division; ");
        write_uint(&file_out, time_div);
        write_text(&file_out, "\n# Volt/div CH1; ");
        write_uint(&file_out, volt_div_ch1);
        write_text(&file_out, "\n# Volt/div CH2; ");
        write_uint(&file_out, volt_div_ch2);
        write_text(&file_out, "\n# Trigger channel; ");
        write_uint(&file_out, trig_ch);
        write_text(&file_out, "\n# Trigger level; ");
        write_uint(&file_out, trig_level);
        write_text(&file_out, "\n# Trigger mode; ");
        write_uint(&file_out, trig_mode);
        write_text(&file_out, "\n# Trigger edge; ");
        write_uint(&file_out, trig_edge);
        write_text(&file_out, "\n#---------------------------------------------\n");
        write_text(&file_out, "Index;CH1;CH2\n");

        // Write channel data
        for (int k = 0; k < CH_SAMPLES; k++)
        {
            write_uint(&file_out, k);
            write_text(&file_out, ";");
            write_uint(&file_out, ch1[k]);
            write_text(&file_out, ";");
            write_uint(&file_out, ch2[k]);
            write_text(&file_out, "\n");
        }

        f_close(&file_out);
    }

    f_closedir(&dir);
    return FR_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
//skusit vyuzit aj na long base, pri stopnuti priebehu
int32 scope_load_REFx_trace_data(void)
{
  //Point to the file numbers
  uint16 *fnptr = (uint16 *)viewfilenumberdata;
  uint32 result;

  //Setup the file name for this view item
  scope_print_file_name(fnptr[viewcurrentindex]);

  //Try to open the file for reading
  result = f_open(&viewfp, viewfilename, FA_READ);

  //Check if file opened ok
  if(result == FR_OK)
  {
    //Checks on correct number of bytes read might be needed
    //Load the setup data to the file setup data buffer
    if((result = f_read(&viewfp, (uint8 *)viewfilesetupdata, sizeof(viewfilesetupdata), 0)) == FR_OK)
    {
      //Copy the loaded data to the settings
      scope_restore_setup_from_file();

      //Check if the version of the file is wrong
      if(viewfilesetupdata[1] != WAVEFORM_FILE_VERSION)
      {
        //No need to load the rest of the data
        result = WAVEFORM_FILE_ERROR;

        //Show the user the file is not correct
        scope_display_file_status_message(MESSAGE_WAV_VERSION_MISMATCH, 0);
      }
      else
      {
        //Load the channel 1 sample data      
        if((result = f_read(&viewfp, (uint8 *)channel1tracebuffer, 3000, 0)) == FR_OK)
        {
          //Load the channel 2 sample data
          if((result = f_read(&viewfp, (uint8 *)channel2tracebuffer, 3000, 0)) == FR_OK)
          {
            //Do a check on file validity
            if((result = scope_check_waveform_file()) == 0)
            {
              //Switch to stopped and waveform viewing mode
              scopesettings.runstate = 0;//ok
              scopesettings.waveviewmode = 1;

              //Show the normal scope screen
              scope_setup_main_screen();

              //display the trace data
              scope_display_trace_data();
            }
            else
            {
              //Checksum error so signal that to the user
              result = WAVEFORM_FILE_ERROR;

              //Show the user the file is not correct
              scope_display_file_status_message(MESSAGE_WAV_CHECKSUM_ERROR, 0);
            }
          }
        }
      }
    }

    //Done with the file so close it
    f_close(&viewfp);

    //Check if one of the reads failed
    if((result != FR_OK) && (result != WAVEFORM_FILE_ERROR))
    {
      //Signal unable to write to the file
      scope_display_file_status_message(MESSAGE_FILE_READ_FAILED, 0);
    }
  }
  else
  {
    //Signal unable to open the file
    scope_display_file_status_message(MESSAGE_FILE_OPEN_FAILED, 0);
  }

  //Check if all went well
  if(result == FR_OK)
  {
    //Tell it to the caller
    return(VIEW_TRACE_LOAD_OK);
  }

  //Remove the current item from the thumbnails and delete the item from disk
  scope_remove_item_from_thumbnails(1);

  //Save the thumbnail file
  scope_save_thumbnail_file();

  return(VIEW_TRACE_LOAD_ERROR);
}

//----------------------------------------------------------------------------------------------------------------------------------


  //if (method == 'Sinc') { y = sincInterpolate(waveArray, tGlobal);}
                    
                
		// Sinc Interpolation with Lanczos window
/*          void sincInterpolate(points, t, a = 6) {
            int32 sum = 0;
            const start = math.max(0, Math.floor(t - a));
            const end = Math.min(points.length - 1, Math.ceil(t + a));
            
            for (int32 i = start; i <= end; i++) {
                const x = i - t;
                // Apply Sinc kernel with Lanczos window
                const sincValue = sincKernel(x);
                const windowValue = lanczosWindow(x, a);
                sum += points[i] * sincValue * windowValue;
            }
            
            return sum;
        }

        function sincKernel(x) {
            if (x == 0) return 1;
            return Math.sin(Math.PI * x) / (Math.PI * x);
        }

        function lanczosWindow(x, a) {
            if (Math.abs(x) >= a) return 0;
            return sincKernel(x / a); // Sinc function scaled by the window size
        }
*/

void display_text_message(char* message, uint8 value) {
    display_set_fg_color(BLACK_COLOR);
    display_fill_rect(488, 420, 250, 50); // Vyčistí priestor na zobrazenie textu

    // Nastavenie farby textu a fontu
    display_set_fg_color(WHITE_COLOR);
    display_set_font(&font_0);

    // Zobrazenie textu
    display_text(488, 440, message);
    display_decimal(600, 440, value);
    display_text(495, 455, viewfilename);

    timer0_delay(3000); // Počká 3 sekundy
}



/*

// Function to compute the value of sin(x)/x for sinc interpolation
double sinc(double x) {
    if (x == 0) {
        return 1.0;
    } else {
        return sin(PI * x) / (PI * x);
    }
}

// Function to perform sinc interpolation
void sinc_interpolation(double* t_interp, double* t_sampled, int* sampled_signal, int* sincpole, int size) {
    for (int i = 0; i < size; i++) {
        sincpole[i] = 0;  // Initialize the interpolated signal
        for (int j = 0; j < size; j++) {
            double sinc_value = sinc((t_interp[i] - t_sampled[j]) / (t_sampled[1] - t_sampled[0]));
            sincpole[i] += sampled_signal[j] * sinc_value;
        }
        // Clip the interpolated value to the range [0, 400] and convert to integer
        if (sincpole[i] < 0) {
            sincpole[i] = 0;
        } else if (sincpole[i] > 400) {
            sincpole[i] = 400;
        }
    }
}
*/
// Funkcia na výpočet pohyblivého priemeru (SMA) s oknom window_size
void moving_average(uint8* input, size_t size, size_t window_size) {
    uint32 sum = 0;  // Použijeme väčší dátový typ pre súčet
    size_t half_window = window_size / 2;
    uint8_t temp[size];  // Dočasné pole pre uloženie výsledkov

    // Vypočítanie priemeru pre každú vzorku signálu
    for (size_t i = 0; i < size; i++) {
        sum = 0;
        size_t count = 0;

        // Vypočíta priemer v okne okolo aktuálneho bodu
        for (int j = -half_window; j <= (int)half_window; j++) {
            if ((i + j) >= 0 && (i + j) < size) {
                sum += input[i + j];
                count++;
            }
        }

        // Uloženie výsledku priemeru do dočasného poľa
        temp[i] = sum / count;
    }

    // Skopírovanie výsledku späť do pôvodného input poľa
    for (size_t i = 0; i < size; i++) {
        input[i] = temp[i];
    }
}

//-------------------------------------------------------------------------------
// Funkcia na výpočet exponenciálneho kĺzavého priemeru (EMA)
void exponential_moving_average(uint8_t* input, size_t size, double alpha) {
    double ema = input[0];  // Inicializácia s prvou vzorkou

    // Iterácia cez všetky vzorky
    for (size_t i = 1; i < size; i++) {
        ema = alpha * input[i] + (1 - alpha) * ema;
        input[i] = (uint8_t)ema;  // Uloženie výsledku priamo späť do input poľa
    }
}

//-------------------------------------------------------------------------------
// Funkcia na filtrovanie signálu pomocou FIR filtra
void fir_filter(uint8_t* input, size_t size) {
    // FIR koeficienty pre dolnopriepustný filter (koeficienty je potrebné vypočítať pre cutoff 49 MHz)
double fir_coeffs[] = {
    -0.001288, -0.000300, 0.001836, 0.000676, -0.003386, -0.001576, 0.006156, 0.003345, -0.010409, -0.006472, 0.016563, 0.011743, -0.025481, -0.020732, 0.039401, 0.037612, -0.066018, -0.079762, 0.155080, 0.443010, 0.443010, 0.155080, -0.079762, -0.066018, 0.037612, 0.039401, -0.020732, -0.025481, 0.011743, 0.016563, -0.006472, -0.010409, 0.003345, 0.006156, -0.001576, -0.003386, 0.000676, 0.001836, -0.000300, -0.001288 // Odraz koeficientov
};

    uint8_t output[MAX_SAMPLE_BUFFER_SIZE]; // Poľe pre filtrovaný signál

    for (size_t n = 0; n < size; n++) {
        double sum = 0.0;
        // Vypočítajte hodnotu pre aktuálny výstup
        for (size_t k = 0; k < FIR_ORDER; k++) {
            if (n >= k) {  // Zabezpečte, že index nie je mimo rozsah
                sum += fir_coeffs[k] * input[n - k];
            }
        }
        output[n] = (uint8_t)sum; // Uložte hodnotu do output poľa
    }

    // Uloženie filtrovaného signálu späť do input poľa (scopesettings.channel1.tracebuffer)
    for (size_t i = 0; i < size; i++) {
        input[i] = output[i]; // Uloženie filtrovaných hodnôt do pôvodného poľa
    }
}

//--------------------------------------------------------------------------------
// Funkcia na filtrovanie zmien v rozsahu 1 až 2 bity
void filter_bit_changes(uint8_t* input, size_t size) {
    for (size_t i = 1; i < size; i++) {
        // Zistíme rozdiel medzi aktuálnou a predchádzajúcou hodnotou
        int16_t difference = (int16_t)input[i] - (int16_t)input[i - 1];

        // Ak je zmena v rozsahu 1 až 2 bity, nastavíme aktuálnu hodnotu na predchádzajúcu
        if (difference >= 1 && difference <= 2) {
            input[i] = input[i - 1];
        } else if (difference <= -1 && difference >= -2) {
            input[i] = input[i - 1];
        }
    }
}
//-----------------------------------------------------------------------------

// Funkcia na pridanie 1 bitu k hodnotám signálu
void add_one_bit(uint8_t* input, size_t size) {
    for (size_t i = 0; i < size; i++) {
        input[i] += 0x01; // Pridajte 1 bit k hodnote
    }
}

//-----------------------------------------------------------------------
// Funkcia na výpočet priemeru v okne
void average_filter(uint8_t* input, size_t size, size_t window_size) {
    if (window_size % 2 == 0) {
        window_size++; // Zabezpečte, že okno je nepárne
    }

    uint32_t sum = 0;  // Používame väčší dátový typ pre súčet
    size_t half_window = window_size / 2;
    uint8_t temp[MAX_SAMPLE_BUFFER_SIZE];  // Dočasné pole pre uloženie výsledkov

    // Pre každý vzor
    for (size_t i = 0; i < size; i++) {
        sum = 0;
        size_t count = 0;

        // Vypočítanie priemeru v okne okolo aktuálneho bodu
        for (int j = -half_window; j <= (int)half_window; j++) {
            size_t index = i + j;

            // Zabezpečte, že index je v rámci povoleného rozsahu
            if (index < size && index >= 0) {
                sum += input[index];
                count++;
            }
        }

        // Uloženie výsledku priemeru do dočasného poľa
        temp[i] = sum / count; // Uložte ako uint8_t
    }

    // Skopírovanie výsledku späť do pôvodného input poľa
    for (size_t i = 0; i < size; i++) {
        input[i] = temp[i];
    }
}

/*
void scope_preset_long_mode(void)
{
 * 
 
display_fill_rect(740,  83, 50, 15);//99
display_fill_rect(740, 248, 50, 15);//264
//Black color for inactive button
display_set_fg_color(BLACK_COLOR);

display_set_font(&font_2);
display_decimal(740, 83, sampleAVG);

}
*/

//----------------------------------------------------------------------------------------------------------------------------------
//**********************************************************************************************************************************
//----------------------------------------------------------------------------------------------------------------------------------

void scope_test_ADoffset_value(void)
{
      display_set_fg_color(BLACK_COLOR);
      //Fill the settings background
      display_fill_rect(550, 0, 100, 220);  //x , y , sirka, vyska
      display_set_fg_color(WHITE_COLOR);
      display_set_font(&font_2);
      //display_text(650, 10, buffertime);
      //uint32 center = (scopesettings.channel1.center * signal_adjusters[scopesettings.channel1.displayvoltperdiv]) >> VOLTAGE_SHIFTER;   
      
       
 // uint32 offset;
  uint32    max1;
  uint32    min1;    
  uint32    p2p1; 
  uint32    center1x; 
  uint32    dcoffset;
  uint32    dcoffsetm;
  
  register uint32 multiply;
  
  PCHANNELSETTINGS settings;
     /* 
    max1 = (scopesettings.channel1.max * scopesettings.channel1.input_calibration[scopesettings.channel1.displayvoltperdiv]) >> VOLTAGE_SHIFTER;
    center1x = (scopesettings.channel1.center * scopesettings.channel1.input_calibration[scopesettings.channel1.displayvoltperdiv]) >> VOLTAGE_SHIFTER;
      //  center1x = (((scopesettings.channel1.center) * multiply ) >> VOLTAGE_SHIFTER);//3289650
    min1= (center1x*scopesettings.channel1.dc_shift_center)/100; 
    
    //min1 = (scopesettings.channel1.min * scopesettings.channel1.input_calibration[scopesettings.channel1.displayvoltperdiv]) >> VOLTAGE_SHIFTER;
    p2p1 = (scopesettings.channel1.peakpeak * scopesettings.channel1.input_calibration[scopesettings.channel1.displayvoltperdiv]) >> VOLTAGE_SHIFTER;
    dcoffset = (scopesettings.channel1.dcoffset * scopesettings.channel1.input_calibration[scopesettings.channel1.displayvoltperdiv]) >> VOLTAGE_SHIFTER;
    dcoffsetm = (scopesettings.channel1.dcoffset * multiply ) >> VOLTAGE_SHIFTER;//3289650
       */
    
    settings = &scopesettings.channel1;
    
    multiply=(settings->input_calibration[settings->samplevoltperdiv]);//*(settings->dc_shift_center)/100; ;
    
    //Get the sample and adjust the data  
    max1 = ((settings->maxDC * multiply ) >> VOLTAGE_SHIFTER);//3289650
    center1x = ((settings->centerDC * multiply ) >> VOLTAGE_SHIFTER);//3289650
    //center1x = settings->center;
    min1 = ((settings->minDC * multiply ) >> VOLTAGE_SHIFTER);//3289650
    p2p1 = ((settings->peakpeakDC * multiply ) >> VOLTAGE_SHIFTER);//3289650
    
    /*
        //Get the sample and adjust the data  
    max1 = settings->maxAS;//3289650
    //center = ((settings->center * multiply ) >> VOLTAGE_SHIFTER);//3289650
    center1x = settings->centerAS;
    min1 = settings->minAS;//3289650
    p2p1 = settings->peakpeakAS;//3289650
    */
    //scopesettings.channel1.dcoffset
      
      display_decimal(550, 0, max1 );
      display_decimal(550, 10, center1x);
      display_decimal(550, 20, min1);
      display_decimal(550, 30, p2p1);
      //display_decimal(550, 40, dcoffset);
      //display_decimal(550, 50, dcoffsetm);
    
      display_decimal(600, 0, scopesettings.channel1.max );
      display_decimal(600, 10, scopesettings.channel1.center );
      display_decimal(600, 20, scopesettings.channel1.min );
      display_decimal(600, 30, scopesettings.channel1.peakpeak );
      display_decimal(600, 40, scopesettings.channel1.dcoffset );
      display_decimal(600, 50, scopesettings.channel1.traceposition);
      
      //display_decimal(630, 20, scopesettings.voltcursor1position );
      //display_decimal(630, 30, scopesettings.voltcursor2position );
}

//----------------------------------------------------------------------------------------------------------------------------------

void scope_test_TP(void)
{
  
  #define   positionX  80//450
  #define   positionY  2  //250
  //Draw the background
  //display_fill_rect(159, 116, 226, 36);
  
  display_set_fg_color(BLACK_COLOR);
  //Fill the settings background
  display_fill_rect(positionX, positionY, 80, 95);  //x , y , sirka, vyska
  //display_fill_rect(600, 0, 100, 45);  //x , y , sirka, vyska
  //display_fill_rect(600, 0, 100, 100);  //x , y , sirka, vyska
  //Display the text with the green color
  //display_set_fg_color(GREEN_COLOR);
  /*
  display_set_fg_color(WHITE_COLOR);
  display_set_font(&font_2);
  display_text(550, 0, "Xtouch");
  display_decimal(550, 10, xtouch_tmp);
  display_text(550, 20, "Ytouch");
  display_decimal(550, 30, ytouch_tmp);//450 320
  display_decimal(550, 40, scopesettings.runstate);//450 320
   */
  
  display_set_fg_color(WHITE_COLOR);
  display_set_font(&font_2);
  display_text(positionX, positionY, "Xtouch");
  display_decimal(positionX, positionY+10, xtouch);
  display_text(positionX, positionY+20, "Ytouch");
  display_decimal(positionX, positionY+30, ytouch);//450 320
   
  display_text(positionX, positionY+50, "Xtouch tmp");
  display_decimal(positionX, positionY+60, xtouch_tmp);
  display_text(positionX, positionY+70, "Ytouch tmp");
  display_decimal(positionX, positionY+80, ytouch_tmp);//450 320
 /* 
  display_text(positionX, positionY+90, "mathmenuopen");
  display_decimal(positionX, positionY+100, mathmenuopen);//450 320
  
  display_text(positionX, positionY+110, "math mode");
  display_decimal(positionX, positionY+120, mathmode);//450 320
  */ 
  /*
  display_set_fg_color(WHITE_COLOR);
  display_set_font(&font_2);
  display_text(600, 0, "Xtouch");
  display_decimal(600, 10, xtouch);
  display_text(600, 20, "Ytouch");
  display_decimal(600, 30, ytouch);//450 320
  */
  /*
  display_text(600, 50, "Xtouch");
  display_decimal(600, 60, xtouch_tmp);
  display_text(600, 70, "Ytouch");
  display_decimal(600, 80, ytouch_tmp);//450 320
  */
  
}


void scope_test_longbase_value(void)
{
      display_set_fg_color(BLACK_COLOR);
      //Fill the settings background
      display_fill_rect(550, 0, 100, 220);  //x , y , sirka, vyska
      display_set_fg_color(WHITE_COLOR);
      display_set_font(&font_2);
      //display_text(650, 10, buffertime);

      display_text(550, 10, "sara"); 
      display_decimal(600, 10, scopesettings.samplerate);
      display_text(550, 20, "tpd");
      display_decimal(600, 20, scopesettings.timeperdiv);
   
      display_decimal(600, 30, frequency_per_div[scopesettings.timeperdiv]);
      display_decimal(600, 40, sample_rate[scopesettings.samplerate]);
      
      display_decimal(600, 50, timebase_settings[scopesettings.timeperdiv]);      
}
//----------------------------------------------------------------------------------------------------------------------------------

void scope_test_trigerposition_value(void)
{
  #define   positionX  600
  #define   positionY  100  //250

      display_set_fg_color(BLACK_COLOR);
      //Fill the settings background
      display_fill_rect(positionX, positionY, 100, 220);  //x , y , sirka, vyska
      display_set_fg_color(WHITE_COLOR);
      display_set_font(&font_2);
      //display_text(650, 10, buffertime);

      //display_decimal(600, 0, triggerposition);
      display_decimal(positionX, positionY+10, disp_xstart);
      display_decimal(positionX, positionY+20, disp_xend);
      display_decimal(positionX, positionY+30, disp_first_sample);
      
      display_decimal(positionX, positionY+50, (disp_xstart * 10000) / 42899);
      display_decimal(positionX, positionY+60, (disp_xend * 10000) / 42899);
      
      display_decimal(positionX, positionY+70, scopesettings.triggerhorizontalposition);
      
      
       // thumbnaildata->disp_xstart = (disp_xstart * 10000) / 42899;
       // thumbnaildata->disp_xend   = (disp_xend * 10000) / 42899;
      
      
      //display_decimal(600, 50,  ((disp_trigger_index - disp_first_sample + 1) * 727.0) / (disp_trigger_index - disp_first_sample >= 0 ? (727.0 / disp_xpos_per_sample) : 1));
      //display_decimal(600, 60,  int32 ((disp_trigger_index - disp_first_sample + 1) * 727.0) / (727.0 / disp_xpos_per_sample));
      //display_decimal(600, 50,  (((1500          ) * 727.0) / (727.0 / disp_xpos_per_sample)+15));
      //display_decimal(600, 60,  (((1500 - 3000 ) * 727.0) / (727.0 / disp_xpos_per_sample)));
      
     // (disp_trigger_index - disp_first_sample + 1) / (727.0 / disp_xpos_per_sample)
      
      //display_decimal(600, 70, disp_trigger_index);
      //display_decimal(600, 80, disp_xpos_per_sample);  

      display_decimal(positionX, positionY+210, scopesettings.samplerate);
         
      display_decimal(positionX, positionY+220, scopesettings.timeperdiv);
            
      //display_decimal(600, 230, frequency_per_div[scopesettings.timeperdiv]);
      
}

//----------------------------------------------------------------------------------------------------------------------------------

void scope_test_calibration_value(void)
{
      display_set_fg_color(BLACK_COLOR);
      //Fill the settings background
      display_fill_rect(550, 0, 100, 250);  //x , y , sirka, vyska 220
      display_set_fg_color(WHITE_COLOR);
      display_set_font(&font_2);
      //display_text(650, 10, buffertime);
  
      display_decimal(600, 40, scopesettings.channel1.dc_calibration_offset[0]);
      display_decimal(600, 50, scopesettings.channel1.dc_calibration_offset[1]);
      display_decimal(600, 60, scopesettings.channel1.dc_calibration_offset[2]);
      display_decimal(600, 70, scopesettings.channel1.dc_calibration_offset[3]);
      display_decimal(600, 80, scopesettings.channel1.dc_calibration_offset[4]);
      display_decimal(600, 90, scopesettings.channel1.dc_calibration_offset[5]);
      display_decimal(600, 100, scopesettings.channel1.dc_calibration_offset[6]);
      
      display_decimal(600, 120, scopesettings.channel2.dc_calibration_offset[0]);
      display_decimal(600, 130, scopesettings.channel2.dc_calibration_offset[1]);
      display_decimal(600, 140, scopesettings.channel2.dc_calibration_offset[2]);
      display_decimal(600, 150, scopesettings.channel2.dc_calibration_offset[3]);
      display_decimal(600, 160, scopesettings.channel2.dc_calibration_offset[4]);
      display_decimal(600, 170, scopesettings.channel2.dc_calibration_offset[5]);
      display_decimal(600, 180, scopesettings.channel2.dc_calibration_offset[6]);
      
      display_decimal(600, 210, scopesettings.channel1.adc1compensation);
      display_decimal(600, 220, scopesettings.channel1.adc2compensation);
      display_decimal(600, 230, scopesettings.channel2.adc1compensation);
      display_decimal(600, 240, scopesettings.channel2.adc2compensation);
      
      display_decimal(650, 40, scopesettings.channel1.input_calibration[0]);
      display_decimal(650, 50, scopesettings.channel1.input_calibration[1]);
      display_decimal(650, 60, scopesettings.channel1.input_calibration[2]);
      display_decimal(650, 70, scopesettings.channel1.input_calibration[3]);
      display_decimal(650, 80, scopesettings.channel1.input_calibration[4]);
      display_decimal(650, 90, scopesettings.channel1.input_calibration[5]);
      display_decimal(650, 100, scopesettings.channel1.input_calibration[6]);
      
      display_decimal(650, 120, scopesettings.channel2.input_calibration[0]);
      display_decimal(650, 130, scopesettings.channel2.input_calibration[1]);
      display_decimal(650, 140, scopesettings.channel2.input_calibration[2]);
      display_decimal(650, 150, scopesettings.channel2.input_calibration[3]);
      display_decimal(650, 160, scopesettings.channel2.input_calibration[4]);
      display_decimal(650, 170, scopesettings.channel2.input_calibration[5]);
      display_decimal(650, 180, scopesettings.channel2.input_calibration[6]);
      
    int32   volts;
    int32   value;
    PVOLTCALCDATA vcd;
    
    //Calculate the voltage based on the channel settings
    vcd = (PVOLTCALCDATA)&volt_calc_data[scopesettings.channel2.magnification][scopesettings.channel2.displayvoltperdiv];
    
    //value=scopesettings.channel1.peakpeak;
    value=scopesettings.channel2.max-128;
    
    volts = (value * scopesettings.channel2.input_calibration[5]) >> VOLTAGE_SHIFTER;
    
    volts *= vcd->mul_factor;
      
    display_decimal(650, 190, value);
    display_decimal(650, 200, volts);
    
    display_decimal(600, 190, scopesettings.channel2.displayvoltperdiv);
    display_decimal(600, 200, scopesettings.channel2.magnification);
    
}
//----------------------------------------------------------------------------------------------------------------------------------