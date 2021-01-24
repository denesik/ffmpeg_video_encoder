
#include <stdint.h>
#include <libavutil\pixfmt.h>

// presets:
//
// ultrafast
// superfast
// veryfast
// faster
// fast
// medium – default preset
// slow
// slower
// veryslow


class FfmpegEncoder
{
public:
	struct Params
	{
		uint32_t width;
		uint32_t height;
		double fps;
		uint32_t bitrate;
		const char *preset;

		uint32_t crf; //0–51

		enum AVPixelFormat src_format;
		enum AVPixelFormat dst_format;
	};

	FfmpegEncoder() = default;
	FfmpegEncoder(const char *filename, const Params &params);
	~FfmpegEncoder();

	bool Open(const char *filename, const Params &params);

	void Close();

	bool Write(const unsigned char *data);

	bool IsOpen() const;

private:
	bool FlushPackets();

private:
	bool mIsOpen = false;

	struct Context
	{
		struct AVFormatContext *format_context = nullptr;
		struct AVStream *stream = nullptr;
		struct AVCodecContext *codec_context = nullptr;
		struct AVFrame *frame = nullptr;
		struct SwsContext *sws_context = nullptr;
		struct AVCodec *codec = nullptr;
		
		uint32_t frame_index = 0;
	};

	Context mContext = {};
};