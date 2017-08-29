#ifndef NSVDECODERTASK_H
#define NSVDECODERTASK_H

typedef struct _NsvDecoderTask NsvDecoderTask;
typedef struct _NsvDecoderTaskPrivate NsvDecoderTaskPrivate;

struct _NsvDecoderTask
{
  GObject parent_instance;
  gchar *category;
  NsvDecoderTaskPrivate *priv;
};

NsvDecoderTask *nsv_decoder_task_new(const gchar *source_file,
                                     const gchar *target_file);
gboolean nsv_decoder_task_start(NsvDecoderTask *self);
void nsv_decoder_task_stop(NsvDecoderTask *self);
#endif // NSVDECODERTASK_H
