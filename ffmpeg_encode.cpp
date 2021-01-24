#include "ffmpeg_encode.h"

#include <iostream>

extern "C"
{
#include <libavformat\avformat.h>
#include <libavutil\rational.h>
#include <libswscale\swscale.h>
#include <libavutil\opt.h>
#include <libavutil\error.h>
}


FfmpegEncoder::FfmpegEncoder(const char *filename, const Params &params)
{
	Open(filename, params);
}

FfmpegEncoder::~FfmpegEncoder()
{
	Close();
}

bool FfmpegEncoder::Open(const char *filename, const Params &params)
{
	Close();

	do 
	{
		avformat_alloc_output_context2(&mContext.format_context, nullptr, nullptr, filename);
		if (!mContext.format_context)
		{
			std::cout << "could not allocate output format" << std::endl;
			break;
		}

		mContext.codec = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (!mContext.codec) 
		{
			std::cout << "could not find encoder" << std::endl;
			break;
		}

		mContext.stream = avformat_new_stream(mContext.format_context, nullptr);
		if (!mContext.stream) 
		{
			std::cout << "could not create stream" << std::endl;
			break;
		}
		mContext.stream->id = (int)(mContext.format_context->nb_streams - 1);

		mContext.codec_context = avcodec_alloc_context3(mContext.codec);
		if (!mContext.codec_context) 
		{
			std::cout << "could not allocate mContext codec context" << std::endl;
			break;
		}

		mContext.codec_context->codec_id = mContext.format_context->oformat->video_codec;
		mContext.codec_context->bit_rate = params.bitrate; 
		mContext.codec_context->width = static_cast<int>(params.width);
		mContext.codec_context->height = static_cast<int>(params.height); 
		mContext.stream->time_base = av_d2q(1.0 / params.fps, 120);
		mContext.codec_context->time_base = mContext.stream->time_base;
		mContext.codec_context->pix_fmt = params.dst_format;
		mContext.codec_context->gop_size = 12;
		mContext.codec_context->max_b_frames = 2;

		if (mContext.format_context->oformat->flags & AVFMT_GLOBALHEADER) 
			mContext.codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		int ret = 0;
		if (params.preset)
		{
			ret = av_opt_set(mContext.codec_context->priv_data, "preset", params.preset, 0);
			if (ret != 0)
			{
				std::cout << "could not set preset: " << params.preset << std::endl;
				break;
			}
		}

		{
			ret = av_opt_set_int(mContext.codec_context->priv_data, "crf", params.crf, 0);
			if (ret != 0)
			{
				std::cout << "could not set crf: " << params.crf << std::endl;
				break;
			}
		}

		ret = avcodec_open2(mContext.codec_context, mContext.codec, nullptr);
		if (ret != 0) 
		{
			std::cout << "could not open codec: " << ret << std::endl;
			break;
		}

		mContext.frame = av_frame_alloc();
		if (!mContext.frame)
		{
			std::cout << "could not allocate mContext frame" << std::endl;
			break;
		}
		mContext.frame->format = mContext.codec_context->pix_fmt;
		mContext.frame->width = mContext.codec_context->width;
		mContext.frame->height = mContext.codec_context->height;

		ret = av_frame_get_buffer(mContext.frame, 32);
		if (ret < 0) 
		{
			std::cout << "could not allocate the mContext frame data" << std::endl;
			break;
		}

		ret = avcodec_parameters_from_context(mContext.stream->codecpar, mContext.codec_context);
		if (ret < 0) 
		{
			std::cout << "could not copy the stream parameters" << std::endl;
			break;
		}

		mContext.sws_context = sws_getContext(
			mContext.codec_context->width, mContext.codec_context->height, params.src_format,   // src
			mContext.codec_context->width, mContext.codec_context->height, params.dst_format, // dst
			SWS_BICUBIC, nullptr, nullptr, nullptr
		);
		if (!mContext.sws_context) 
		{
			std::cout << "could not initialize the conversion context" << std::endl;
			break;
		}

		av_dump_format(mContext.format_context, 0, filename, 1);

		ret = avio_open(&mContext.format_context->pb, filename, AVIO_FLAG_WRITE);
		if (ret != 0) 
		{
			std::cout << "could not open " << filename << std::endl;
			break;
		}

		ret = avformat_write_header(mContext.format_context, nullptr);
		if (ret < 0)
		{
			std::cout << "could not write" << std::endl;
			ret = avio_close(mContext.format_context->pb);
			if (ret != 0)
				std::cout << "failed to close file" << std::endl;
			break;
		}

		mContext.frame_index = 0;
		mIsOpen = true;
		return true;
	} while (false);

	Close();

	return false;
}

void FfmpegEncoder::Close()
{
	if (mIsOpen)
	{
		avcodec_send_frame(mContext.codec_context, nullptr);

		FlushPackets();

		av_write_trailer(mContext.format_context);

		auto ret = avio_close(mContext.format_context->pb);
		if (ret != 0)
			std::cout << "failed to close file" << std::endl;
	}

	if (mContext.sws_context)
		sws_freeContext(mContext.sws_context);

	if (mContext.frame)
		av_frame_free(&mContext.frame);

	if (mContext.codec_context)
		avcodec_free_context(&mContext.codec_context);

	if (mContext.codec_context)
		avcodec_close(mContext.codec_context);

	if (mContext.format_context)
		avformat_free_context(mContext.format_context);

	mContext = {};
	mIsOpen = false;
}

bool FfmpegEncoder::Write(const unsigned char *data)
{
	if (!mIsOpen)
		return false;

	auto ret = av_frame_make_writable(mContext.frame);
	if (ret < 0)
	{
		std::cout << "frame not writable" << std::endl;
		return false;
	}

	const int in_linesize[1] = { mContext.codec_context->width * 3 };

	sws_scale(
		mContext.sws_context,
		&data, in_linesize, 0, mContext.codec_context->height,  // src
		mContext.frame->data, mContext.frame->linesize // dst
	);
	mContext.frame->pts = mContext.frame_index++;

	ret = avcodec_send_frame(mContext.codec_context, mContext.frame);
	if (ret < 0) 
	{
		std::cout << "error sending a frame for encoding" << std::endl;
		return false;
	}

	return FlushPackets();
}

bool FfmpegEncoder::IsOpen() const
{
	return mIsOpen;
}


bool FfmpegEncoder::FlushPackets()
{
	int ret;
	do
	{
		AVPacket packet = { 0 };

		ret = avcodec_receive_packet(mContext.codec_context, &packet);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;

		if (ret < 0)
		{
			std::cout << "error encoding a frame: " << ret << std::endl;
			return false;
		}

		av_packet_rescale_ts(&packet, mContext.codec_context->time_base, mContext.stream->time_base);
		packet.stream_index = mContext.stream->index;

		ret = av_interleaved_write_frame(mContext.format_context, &packet);
		av_packet_unref(&packet);
		if (ret < 0)
		{
			std::cout << "error while writing output packet: " << ret << std::endl;
			return false;
		}
	} while (ret >= 0);

	return true;
}

