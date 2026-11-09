// Test helper: Qt's JPEG writer bakes rotation into pixels and never writes an EXIF tag,
// so real camera-style files have to be faked by inserting an APP1/Orientation segment.
#pragma once

#include <QByteArray>

inline QByteArray withExifOrientation(QByteArray jpeg, int orientation) {
  QByteArray app1;
  app1.append("\xFF\xE1\x00\x22", 4);                  // APP1, length 34
  app1.append("Exif\0\0", 6);
  app1.append("MM\x00\x2A\x00\x00\x00\x08", 8);         // big-endian TIFF, IFD0 at offset 8
  app1.append("\x00\x01", 2);                           // one entry
  app1.append("\x01\x12\x00\x03\x00\x00\x00\x01", 8);   // Orientation, SHORT, count 1
  app1.append(char(0)).append(char(orientation)).append(char(0)).append(char(0));
  app1.append("\x00\x00\x00\x00", 4);                   // no next IFD
  jpeg.insert(2, app1);                                 // right after SOI
  return jpeg;
}
