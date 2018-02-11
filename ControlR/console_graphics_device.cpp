
#include "console_graphics_device.h"
#include "variable.pb.h"

extern void PushConsoleMessage(google::protobuf::Message &message);
extern bool ConsoleCallback(const BERTBuffers::CallResponse &call, BERTBuffers::CallResponse &response);

void SetMessageContext(BERTBuffers::GraphicsContext *target, const pGEcontext gc) {

  //target->set_col(gc->col);
  //target->set_fill(gc->fill);

  /*
    inline Gdiplus::ARGB RColor2ARGB(int color) {
    return (color & 0xff00ff00) | ((color >> 16) & 0x000000ff) | ((color << 16) & 0x00ff0000);
  }
  */

  auto color = target->mutable_col();
  color->set_a(((gc->col >> 24) & 0xff));
  color->set_r(((gc->col >> 8) & 0xff));
  color->set_g(((gc->col >> 0) & 0xff));
  color->set_b(((gc->col >> 16) & 0xff));

  auto fill = target->mutable_fill();
  fill->set_a(((gc->fill >> 24) & 0xff));
  fill->set_r(((gc->fill >> 8) & 0xff));
  fill->set_g(((gc->fill >> 0) & 0xff));
  fill->set_b(((gc->fill >> 16) & 0xff));

  target->set_gamma(gc->gamma);
  target->set_lwd(gc->lwd);
  target->set_lty(gc->lty);
  target->set_lend(gc->lend);
  target->set_ljoin(gc->ljoin);
  target->set_lmitre(gc->lmitre);
  target->set_cex(gc->cex);
  target->set_ps(gc->ps);
  target->set_lineheight(gc->lineheight);
  target->set_fontface(gc->fontface); // can unpack into flags
  target->set_fontfamily(gc->fontfamily);
}

void set_clip(double x0, double x1, double y0, double y1, pDevDesc dd) {
  //std::cout << "g: set clip" << std::endl;
 
  BERTBuffers::CallResponse message;
  auto graphics = message.mutable_console()->mutable_graphics();
  // SetMessageContext(graphics->mutable_context(), gc);

  graphics->set_command("set-clip");
  graphics->add_x(x0);
  graphics->add_y(y0);
  graphics->add_x(x1);
  graphics->add_y(y1);

  PushConsoleMessage(message);

}

void new_page(const pGEcontext gc, pDevDesc dd) {
  // std::cout << "g: new page: " << dd->right << ", " << dd->bottom << std::endl;
  BERTBuffers::CallResponse message;
  auto graphics = message.mutable_console()->mutable_graphics();
  SetMessageContext(graphics->mutable_context(), gc);

  graphics->set_command("new-page");
  graphics->add_x(dd->right);
  graphics->add_y(dd->bottom);

  PushConsoleMessage(message);
}

void close_device(pDevDesc dd) {
}

void draw_line(double x1, double y1, double x2, double y2, const pGEcontext gc, pDevDesc dd) {
  // std::cout << "g: draw line" << std::endl;
  BERTBuffers::CallResponse message;
  auto graphics = message.mutable_console()->mutable_graphics();
  SetMessageContext(graphics->mutable_context(), gc);

  graphics->set_command("draw-line");
  graphics->add_x(x1);
  graphics->add_x(x2);
  graphics->add_y(y1);
  graphics->add_y(y2);
  PushConsoleMessage(message);
}

void draw_poly(int n, double *x, double *y, bool filled, const pGEcontext gc, pDevDesc dd) {

  BERTBuffers::CallResponse message;
  auto graphics = message.mutable_console()->mutable_graphics();
  SetMessageContext(graphics->mutable_context(), gc);

  graphics->set_command("draw-polyline");
  graphics->set_filled(filled);
  for (int i = 0; i < n; i++) {
    graphics->add_x(x[i]);
    graphics->add_y(y[i]);
  }
  PushConsoleMessage(message);

}

void draw_polyline(int n, double *x, double *y, const pGEcontext gc, pDevDesc dd) {
  // std::cout << "g: draw polyline" << std::endl;
  draw_poly(n, x, y, false, gc, dd);
}

void draw_polygon(int n, double *x, double *y, const pGEcontext gc, pDevDesc dd) {
  //std::cout << "g: draw polygon" << std::endl;
  draw_poly(n, x, y, true, gc, dd);
}

void draw_path(double *x, double *y,
  int npoly, int *nper,
  Rboolean winding,
  const pGEcontext gc, pDevDesc dd) {
  std::cerr << "ENOTIMPL: draw_path" << std::endl; // FIXME
}

double get_strWidth(const char *str, const pGEcontext gc, pDevDesc dd) {

  BERTBuffers::CallResponse message, response;
  auto graphics = message.mutable_console()->mutable_graphics();
  SetMessageContext(graphics->mutable_context(), gc);
  graphics->set_command("measure-text"); 
  graphics->set_text(str);
  
  bool success = ConsoleCallback(message, response);
  if (!success) return 10; // default?

  if (response.operation_case() == BERTBuffers::CallResponse::OperationCase::kConsole) {
    auto graphics = response.console().graphics();
    if (graphics.x_size()) {
      return graphics.x(0);
    }
  }

  double w, h;
  w = 10; // FIXME
  return w;
}

void draw_rect(double x1, double y1, double x2, double y2,
  const pGEcontext gc, pDevDesc dd) {
  //std::cout << "g: draw rect" << std::endl;
  BERTBuffers::CallResponse message;
  auto graphics = message.mutable_console()->mutable_graphics();
  SetMessageContext(graphics->mutable_context(), gc);

  graphics->set_command("draw-rect");
  graphics->add_x(x1);
  graphics->add_x(x2);
  graphics->add_y(y1);
  graphics->add_y(y2);
  PushConsoleMessage(message);
}

void draw_circle(double x, double y, double r, const pGEcontext gc,
  pDevDesc dd) {
  //std::cout << "g: draw circle" << std::endl;
  BERTBuffers::CallResponse message;
  auto graphics = message.mutable_console()->mutable_graphics();
  SetMessageContext(graphics->mutable_context(), gc);

  graphics->set_command("draw-circle");
  graphics->add_x(x);
  graphics->add_y(y);
  graphics->set_r(r);
  PushConsoleMessage(message);
}

void draw_text(double x, double y, const char *str, double rot,
  double hadj, const pGEcontext gc, pDevDesc dd) {
  //std::cout << "g: draw text" << std::endl;
  BERTBuffers::CallResponse message;
  auto graphics = message.mutable_console()->mutable_graphics();
  SetMessageContext(graphics->mutable_context(), gc);

  graphics->set_command("draw-text");
  graphics->add_x(x);
  graphics->add_y(y);
  graphics->set_rot(rot);
  graphics->set_hadj(hadj);
  graphics->set_text(str);
  PushConsoleMessage(message);
}

void get_size(double *left, double *right, double *bottom, double *top, pDevDesc dd) {
  // std::cout << "g: get size (" << dd->left << ", " << dd->top << ", " << dd->right << ", " << dd->bottom << ")" << std::endl;

  *left = dd->left;
  *right = dd->right;
  *bottom = dd->bottom;
  *top = dd->top;
}


void get_metric_info(int c, const pGEcontext gc, double* ascent, double* descent, double* width, pDevDesc dd) {

  static char str[8];

  std::cout << "get_metric_info" << std::endl;
  
  // this one can almost certainly do some caching.


  // Convert to string - negative implies unicode code point
  if (c < 0) {
    Rf_ucstoutf8(str, (unsigned int)-c);
  }
  else {
    str[0] = (char)c;
    str[1] = '\0';
  }

  double w, h;

  w = 10; // FIXME
  h = 10; // FIXME

          // fudging a little

  *width = w;
  *ascent = h + .5;
  *descent = 0;
}

void draw_raster(unsigned int *raster,
  int pixel_width, int pixel_height,
  double x, double y,
  double target_width, double target_height,
  double rot,
  Rboolean interpolate,
  const pGEcontext gc, pDevDesc dd) {

  std::cout << "draw raster" << std::endl;
  BERTBuffers::CallResponse message;
  auto graphics = message.mutable_console()->mutable_graphics();
  SetMessageContext(graphics->mutable_context(), gc);

  graphics->set_command("draw-raster");

  // this is contract

  graphics->add_x(x);
  graphics->add_y(y);

  graphics->add_x(pixel_width);
  graphics->add_y(pixel_height);

  graphics->add_x(target_width);
  graphics->add_y(target_height);

  graphics->set_rot(rot);
  graphics->set_interpolate(interpolate);

  std::cout << "raster: " << std::dec << pixel_width << " x " << pixel_height << " * 4 = " << pixel_width*pixel_height * 4 << std::endl;

  // if we want to do any bit/byte/format conversion, we should do 
  // it here rather than on the client, we should be more efficient.

  graphics->set_raster(raster, sizeof(unsigned int) * pixel_width * pixel_height); // int?

  PushConsoleMessage(message);
}

void InitConsoleGraphicsDevice(const std::string &name, void *p) {

  pDevDesc dd = (pDevDesc)p;

  dd->newPage = &new_page;
  dd->close = &close_device;
  dd->clip = &set_clip;

  dd->size = &get_size;
  dd->metricInfo = &get_metric_info;
  dd->strWidth = &get_strWidth;

  dd->line = &draw_line;
  dd->text = &draw_text;
  dd->rect = &draw_rect;
  dd->circle = &draw_circle;
  dd->polygon = &draw_polygon;
  dd->polyline = &draw_polyline;
  dd->path = &draw_path;
  dd->raster = &draw_raster;

  dd->textUTF8 = &draw_text;
  dd->strWidthUTF8 = &get_strWidth;

  // testing

  dd->cra[1] = 1.1 * dd->startps; // pointsize

                                  /*
                                  dd->cra[0] = 0.9 * pointsize;
                                  dd->cra[1] = 1.2 * pointsize;
                                  dd->xCharOffset = 0.4900;
                                  dd->yCharOffset = 0.3333;
                                  dd->yLineBias = 0.2;
                                  */

                                  // ok

  double width = dd->right;
  double height = dd->bottom;

  std::cout << "init device: " << width << ", " << height << std::endl;

  if (!width) width = 400;
  if (!height) height = 400;

  dd->deviceSpecific = 0; //

}

