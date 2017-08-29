#ifndef NSVDECODER_H
#define NSVDECODER_H

typedef struct _NsvDecoder NsvDecoder;

NsvDecoder *nsv_decoder_new();
gchar *nsv_decoder_get_decoded_filename(NsvDecoder *self, const gchar *target_file);
void nsv_decoder_decode(NsvDecoder *self, const gchar *category, const gchar *source_file);

#endif // NSVDECODER_H
