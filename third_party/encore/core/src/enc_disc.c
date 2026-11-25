/* enc_disc_* (core/include/encore/platform.h) for every target, over the disc image layer (enc_discimage.c,
 * encore/discimage.h). */
#include "encore/platform.h"

#include <stdlib.h>

#include "encore/discimage.h"

struct EncDisc {
  EncDimg d;
};

int enc_disc_open(EncDisc **disc, const char *path) {
  EncDisc *e;
  if (!disc || !path) return ENC_ERR_ARG;
  *disc = NULL;
  e = (EncDisc *)calloc(1, sizeof *e);
  if (!e) return ENC_ERR_NOMEM;
  if (enc_dimg_open_any(&e->d, path, 0) != 0) {
    free(e);
    return ENC_ERR_IO;
  }
  *disc = e;
  return ENC_OK;
}

int enc_disc_read(EncDisc *disc, int lba, int count, void *buf) {
  if (!disc || !buf || lba < 0 || count < 0) return ENC_ERR_ARG;
  return enc_dimg_read(&disc->d, lba, count, buf) == 0 ? ENC_OK : ENC_ERR_IO;
}

int enc_disc_find(EncDisc *disc, const char *iso_path, uint32_t *lba, uint32_t *size) {
  unsigned l, s;
  if (!disc || !iso_path) return ENC_ERR_ARG;
  if (enc_dimg_find(&disc->d, iso_path, &l, &s) != 0) return ENC_ERR_IO;
  if (lba) *lba = l;
  if (size) *size = s;
  return ENC_OK;
}

void enc_disc_close(EncDisc *disc) {
  if (!disc) return;
  enc_dimg_close(&disc->d);
  free(disc);
}
