/*
    IIP JTL extended Command Handler Class Member Function: Export appended tiles

    Copyright (C) 2006-2021 Ruven Pillay.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "Task.h" 
#include "Transforms.h"
#include <vips/vips8>

#include <cmath>
#include <sstream>
#include <algorithm>
#include <memory>

#include "mz.h"
#include "mz_strm.h"
#include "mz_strm_mem.h"
#include "mz_zip.h"
#include "mz_os.h"
#include "mz_zip_rw.h"

using namespace std;

bool contains(const vector<int> &vector, int element);

void JTL_Ext::send(Compressor *compressor,
                   const vector<CompressedTile> &compressedTiles,
                   const vector<int> &invalidPathIndices)
{
  stringstream dataStream;
  int tileCount = compressedTiles.size() + invalidPathIndices.size();

  char *buffer;
  size_t bufferSize = 0;
  // Append images vertically if needed
  if (tileCount > 1)
  {
    if (session->loglevel >= 2)
    {
      *(session->logfile) << "JTLExt :: Appending tiles" << endl;
    }
    Timer appendTimer;
    appendTimer.start();
    if (session->loglevel >= 2)
    {
      *(session->logfile) << "JTLExt :: Starting append images timer" << endl;
    }

    using namespace vips;
    if (VIPS_INIT("")) {
          vips_error_exit (NULL);
    }

    // Find width and height of tile
    int tileWidth = compressedTiles[0].rawtile.width;
    int tileHeight = compressedTiles[0].rawtile.height;

    vector<VImage> imagesToAppend;
    int compressedI = 0;
    for (int i = 0; i < tileCount; i++)
    {
      if (!contains(invalidPathIndices, i))
      {
        imagesToAppend.emplace_back(VImage::new_from_buffer(compressedTiles[compressedI].rawtile.data,
                                                            compressedTiles[compressedI].compressedLen,
                                                            nullptr, nullptr));
        compressedI++;
      } else {
        imagesToAppend.emplace_back(VImage::black(tileWidth, tileHeight));
      }
    }
    VImage out = imagesToAppend[0];
    for (int i = 1; i < tileCount; i++) {
      out = out.join(imagesToAppend[i], VIPS_DIRECTION_VERTICAL, nullptr);
    }
    CompressionType compressionType = compressor->getCompressionType();
    string format = compressionType == JPEG ? ".jpg" : ".png";
    out.write_to_buffer(format.c_str(), (void**) &buffer, &bufferSize, nullptr);
    if (session->loglevel >= 2) {
          *(session->logfile) << "JTLExt :: Append images finished in " << appendTimer.getTime() << " milliseconds." << endl;
    }
  }

  // Send image data
  if (compressedTiles.size() == 1 && !invalidPathIndices.size()) {
    stringstream header;
    header << session->response->createHTTPHeader(compressor->getMimeType(), "", compressedTiles[0].compressedLen);
    if (session->out->putStr((const char *)header.str().c_str(), header.tellp()) == -1) {
      if (session->loglevel >= 1) {
        *(session->logfile) << "JTLExt :: Error writing HTTP header" << endl;
      }
    }
    dataStream.write(static_cast<const char *>(compressedTiles[0].rawtile.data),
                     compressedTiles[0].compressedLen);
  } else {
    stringstream header;
    header << session->response->createHTTPHeader(compressor->getMimeType(), "", bufferSize);
    if (session->out->putStr((const char *)header.str().c_str(), header.tellp()) == -1) {
      if (session->loglevel >= 1) {
        *(session->logfile) << "JTLExt :: Error writing HTTP header" << endl;
      }
    }
    dataStream.write(static_cast<const char *>(buffer), bufferSize);
    free(buffer);
  }
  session->out->putStr(dataStream.str().c_str(), dataStream.tellp());

  if (session->out->flush() == -1) {
    if (session->loglevel >= 1) {
      *(session->logfile) << "JTLExt :: Error flushing tile" << endl;
    }
  }
  // Inform our response object that we have sent something to the client
  session->response->setImageSent();
}

void JTL_Ext::sendZip(Compressor *compressor,
                   const vector<CompressedTile> &compressedTiles,
                   const vector<int> &invalidPathIndices,
                   const )
{
  int tileCount = compressedTiles.size() + invalidPathIndices.size();

  int32_t err = MZ_OK;
  void *mem_stream = NULL;
  void *handle = NULL;

  mz_stream_mem_create(&mem_stream);
  mz_stream_mem_set_grow_size(mem_stream, (16 * 1024));
  mz_stream_open(mem_stream, NULL, MZ_OPEN_MODE_CREATE);

  mz_zip_writer_create(&handle);
  err = mz_zip_writer_open(handle, mem_stream, 0); //0 - create
  if (err != MZ_OK) {
   *(session->logfile) << "JTLExt :: Failed to open zip writer! Error: " << err << endl;
  }

  CompressionType compressionType = compressor->getCompressionType();
  string format = compressionType == JPEG ? ".jpg" : ".png";
  int compressedI = 0;
  for (int i = 0; i < tileCount; i++) {

    mz_zip_file file_info = { 0 };
    char buff[20];
    snprintf(buff, sizeof(buff), "t%d%s", i, format.c_str());
    file_info.filename = buff;
    file_info.modified_date = time(NULL);
    file_info.version_madeby = MZ_VERSION_MADEBY;
    file_info.compression_method = MZ_COMPRESS_METHOD_STORE;
    file_info.flag = MZ_ZIP_FLAG_UTF8;

    err == mz_zip_writer_entry_open(handle, &file_info);
    if (err != MZ_OK) {
      *(session->logfile) << "JTLExt :: Unable to create a zip entry! Error: " << err << endl;
    } else {
      if (!contains(invalidPathIndices, i)) {
        int32_t bytes_written = mz_zip_writer_entry_write(
          handle, 
          compressedTiles[compressedI].rawtile.data,
          compressedTiles[compressedI].compressedLen);
        if (bytes_written != compressedTiles[compressedI].compressedLen) {
           *(session->logfile) << "JTLExt :: Zip entry write failed! Written " << bytes_written << ". Error: " << err << endl;
        }
        compressedI++;
      } else {
        const unsigned char *empty = 0x00;
        mz_zip_writer_entry_write(handle, empty, 0);
      }
      err = mz_zip_writer_entry_close(handle);
      if (err != MZ_OK) {
         *(session->logfile) << "JTLExt :: Failed to write zip entry! Error: " << err << endl;
      }
    }
  }

  err = mz_zip_writer_close(handle);
  if (err != MZ_OK) {
    *(session->logfile) << "JTLExt :: Failed to close zip! Error: " << err << endl;
  }

  int32_t mem_stream_size = 0;
  mz_stream_mem_seek(mem_stream, 0, 0);
  mz_stream_mem_get_buffer_length(mem_stream, &mem_stream_size);
  *(session->logfile) << "JTLExt :: Total size: " << mem_stream_size << endl;

  stringstream header;
  header << session->response->createHTTPHeader("application/octet-stream", "", mem_stream_size);
  if (session->out->putStr((const char *)header.str().c_str(), header.tellp()) == -1) {
    if (session->loglevel >= 1) {
      *(session->logfile) << "JTLExt :: Error writing HTTP header" << endl;
    }
  }
  *(session->logfile) << "JTLExt :: Write stream size   " << mem_stream_size << endl;

  std::unique_ptr<char[]> out_buff(new char[mem_stream_size]);
  mz_stream_mem_seek(mem_stream, 0, 0);
  char* buff_ptr = out_buff.get();
  if ((err = mz_stream_mem_read(mem_stream, buff_ptr, mem_stream_size)) != mem_stream_size) {
    *(session->logfile) << "JTLExt :: Output reading failed! Read only " << err << " out of " << mem_stream_size << endl;
  }
  session->out->putStr(buff_ptr, mem_stream_size);

  mz_zip_writer_delete(&handle);
  mz_stream_mem_close(mem_stream);
  mz_stream_mem_delete(&mem_stream);

  if (session->out->flush() == -1) {
    if (session->loglevel >= 1) {
      *(session->logfile) << "JTLExt :: Error flushing tile" << endl;
    }
  }
  // Inform our response object that we have sent something to the client
  session->response->setImageSent();
}

bool contains(const vector<int> &vector, int element)
{
    bool result = false;
    if( find(vector.begin(), vector.end(), element) != vector.end() )
    {
        result = true;
    }
    return result;
}

CompressedTile JTL_Ext::getTile(Session *session, int resolution, int tile)
{
  Timer function_timer;

  if (session->loglevel >= 3) {
    (*session->logfile) << "JTLExt handler reached" << endl;
  }

  // Make sure we have set our image
  this->session = session;
  checkImage();

  // Time this command
  if (session->loglevel >= 2)
    command_timer.start();

  // Sanity check
  if ((resolution < 0) || (tile < 0)) {
    ostringstream error;
    error << "JTLExt :: Invalid resolution/tile number: " << resolution << "," << tile;
    throw error.str();
  }

  // Determine which output encoding to use
  CompressionType ct = session->view->output_format;
  Compressor *compressor;
  if (session->view->output_format == PNG)
    compressor = session->png;
  else
    compressor = session->jpeg;

  TileManager tilemanager(session->tileCache, *session->image, session->watermark, compressor, session->logfile, session->loglevel);

  // Request uncompressed tile if raw pixel data is required for processing
  if ((*session->image)->getNumBitsPerPixel() > 8 || (*session->image)->getColourSpace() == CIELAB ||
    (*session->image)->getNumChannels() == 2 || (*session->image)->getNumChannels() > 3 ||
    ((session->view->colourspace == GREYSCALE || session->view->colourspace == BINARY)
    && (*session->image)->getNumChannels() == 3 && (*session->image)->getNumBitsPerPixel() == 8) ||
    session->view->floatProcessing() || session->view->equalization || session->view->getRotation() != 0.0 ||
    session->view->flip != 0 ) {
    ct = UNCOMPRESSED;
  }

  // Set the physical output resolution for this particular view and zoom level
  int num_res = (*session->image)->getNumResolutions();
  unsigned int im_width = (*session->image)->image_widths[num_res - resolution - 1];
  unsigned int im_height = (*session->image)->image_heights[num_res - resolution - 1];
  float dpi_x = (*session->image)->dpi_x * (float)im_width / (float)(*session->image)->getImageWidth();
  float dpi_y = (*session->image)->dpi_y * (float)im_height / (float)(*session->image)->getImageHeight();
  compressor->setResolution(dpi_x, dpi_y, (*session->image)->dpi_units);

  if (session->loglevel >= 5) {
    *(session->logfile) << "JTLExt :: Setting physical resolution of tile to " << dpi_x << " x " << dpi_y
                        << (((*session->image)->dpi_units == 1) ? " pixels/inch" : " pixels/cm") << endl;
  }

  // Embed ICC profile
  if (session->view->embedICC() && ((*session->image)->getMetadata("icc").size() > 0)) {
    if (session->loglevel >= 3) {
      *(session->logfile) << "JTLExt :: Embedding ICC profile with size "
                          << (*session->image)->getMetadata("icc").size() << " bytes" << endl;
    }
    compressor->setICCProfile((*session->image)->getMetadata("icc"));
  }

  RawTile rawtile = tilemanager.getTile(resolution, tile, session->view->xangle,
                                        session->view->yangle, session->view->getLayers(), ct);

  int len = rawtile.dataLength;

  if (session->loglevel >= 2) {
    *(session->logfile) << "JTLExt :: Tile size: " << rawtile.width << " x " << rawtile.height << endl
                        << "JTLExt :: Channels per sample: " << rawtile.channels << endl
                        << "JTLExt :: Bits per channel: " << rawtile.bpc << endl
                        << "JTLExt :: Data size is " << len << " bytes" << endl;
  }

  // Convert CIELAB to sRGB
  if ((*session->image)->getColourSpace() == CIELAB) {
    if (session->loglevel >= 4) {
      *(session->logfile) << "JTLExt :: Converting from CIELAB->sRGB";
      function_timer.start();
    }
    session->processor->LAB2sRGB(rawtile);
    if (session->loglevel >= 4) {
      *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
    }
  }

  // Reduce to 1 or 3 bands if we have an alpha channel or a multi-band image and have requested a JPEG tile
  // For PNG, strip extra bands if we have more than 4 present
  if (((session->view->output_format == JPEG) && (rawtile.channels == 2 || rawtile.channels > 3)) ||
      ((session->view->output_format == PNG) && (rawtile.channels > 4))) {
    unsigned int bands = (rawtile.channels == 2) ? 1 : 3;
    if (session->loglevel >= 4) {
      *(session->logfile) << "JTLExt :: Flattening channels to " << bands;
      function_timer.start();
    }
    session->processor->flatten(rawtile, bands);
    if (session->loglevel >= 4) {
      *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
    }
  }
  
  // Convert to greyscale if requested
  if ((*session->image)->getColourSpace() == sRGB && session->view->colourspace == GREYSCALE) {
    if (session->loglevel >= 4) {
      *(session->logfile) << "JTLExt :: Converting to greyscale";
      function_timer.start();
    }
    session->processor->greyscale(rawtile);
    if (session->loglevel >= 4) {
      *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
    }
  }

  // Compress to requested output format
  if (rawtile.compressionType == UNCOMPRESSED) {
    if (session->loglevel >= 4) {
      *(session->logfile) << "JTLExt :: Encoding UNCOMPRESSED tile";
      function_timer.start();
    }
    len = compressor->Compress(rawtile);
    if (session->loglevel >= 4) {
      *(session->logfile) << " in " << function_timer.getTime() << " microseconds to "
                          << rawtile.dataLength << " bytes" << endl;
    }
  }
  CompressedTile compressedTile;
  compressedTile.rawtile = rawtile;
  compressedTile.compressedLen = len;
  return compressedTile;
}