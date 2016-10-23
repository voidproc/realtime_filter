#include <Siv3D.hpp>

// Waveに対するローパスフィルタ リアルタイム処理
//
// 参考：
//     http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
// 
// 操作：
//     Left/Right : カットオフ周波数の増減
//     Down/Up    : Q値の増減
//     Space      : 再生／一時停止
//     オーディオファイルのドロップ : ファイルの読み込み・再生
//     シークバー（画面上部）のクリック : シーク


struct Filter
{
    double k0, k1, k2, k3, k4;
    double s, q, f;

    Filter(const int samplingRate, const double f, const double q) : s(samplingRate), q(q), f(f)
    {
        calc();
    }

    void calc()
    {
        const double omg = 2.0_pi * f / s;
        const double alpha = Sin(omg) / (2 * q);

        const double b0 = (1 - Cos(omg)) / 2;
        const double b1 = 1 - Cos(omg);
        const double b2 = (1 - Cos(omg)) / 2;
        const double a0 = 1 + alpha;
        const double a1 = -2 * Cos(omg);
        const double a2 = 1 - alpha;

        k0 = b0 / a0;
        k1 = b1 / a0;
        k2 = b2 / a0;
        k3 = -(a1 / a0);
        k4 = -(a2 / a0);
    }
};


struct OutputBuffer
{
    double yl1 = 0;
    double yl2 = 0;
    double yr1 = 0;
    double yr2 = 0;
};


int32 filteredSampleLeft(const Wave& wave, const size_t pos, const Filter& filter, const OutputBuffer& out)
{
    int32 w1 = (pos > 0) ? wave[pos - 1].left : 0;
    int32 w2 = (pos > 1) ? wave[pos - 2].left : 0;

    int32 y = filter.k0 * wave[pos].left
        + filter.k1 * w1
        + filter.k2 * w2
        + filter.k3 * out.yl1
        + filter.k4 * out.yl2;

    return Clamp(y, -32768, 32767);
}

int32 filteredSampleRight(const Wave& wave, const size_t pos, const Filter& filter, const OutputBuffer& out)
{
    int32 w1 = (pos > 0) ? wave[pos - 1].right : 0;
    int32 w2 = (pos > 1) ? wave[pos - 2].right : 0;

    int32 y = filter.k0 * wave[pos].right
        + filter.k1 * w1
        + filter.k2 * w2
        + filter.k3 * out.yr1
        + filter.k4 * out.yr2;

    return Clamp(y, -32768, 32767);
}

void applyFilter(Sound& sound, Wave& wave, const Wave& waveOrig, const size_t start, const size_t length, const Filter& filter, OutputBuffer& out)
{
    for (size_t i = start; i < Min((int64)(start + length), sound.lengthSample()); i++)
    {
        const int32 yl = filteredSampleLeft(waveOrig, i, filter, out);
        const int32 yr = filteredSampleRight(waveOrig, i, filter, out);
        wave[i].left = yl;
        wave[i].right = yr;
        out.yl2 = out.yl1;
        out.yl1 = yl;
        out.yr2 = out.yr1;
        out.yr1 = yr;
    }

    sound.fill(start, &wave[start], length);
}


struct FilteredBlock
{
	BoolArray block;

	FilteredBlock(const size_t lengthSample, const size_t bufferLength)
	{
		block.resize(lengthSample / bufferLength + ((lengthSample % bufferLength) ? 1 : 0), false);
	}

	bool& operator[](size_t i)
	{
		return block[i];
	}

	void reset()
	{
		for (bool& b : block) { b = false; }
	}
};


void Main()
{
	// Init Siv3D
	Window::SetTitle(L"realtime_filter");
    Window::Resize(640, 480);

	// Assets
	Font fontHz(48, Typeface::Heavy);
	Font fontQ(28, Typeface::Heavy);
	Wave wave(L"Example/風の丘.mp3");
	Wave waveOrig(wave);
	Sound sound(wave, SoundLoop::All);

	int64 len = sound.lengthSample();

	// Filter
	Filter filter(wave.samplingRate, 1500, 1.0);
	OutputBuffer out;
	const size_t BufLen = 5000;
	FilteredBlock block(wave.lengthSample, BufLen);

	applyFilter(sound, wave, waveOrig, 0, BufLen, filter, out);
    block[0] = true;

	// Play default sound
    //sound.play();


	while (System::Update())
	{
        const int64 pos = sound.streamPosSample();

		// Play / Pause

		if (Input::KeySpace.clicked)
		{
			if (!sound.isPlaying() || sound.isPaused())
			{
				sound.play();
			}
			else {
				sound.pause();
			}
		}


		// Change filter freq.

        if (Input::KeyLeft.pressed)
        {
			filter.f -= 8;
        }

        if (Input::KeyRight.pressed)
        {
			filter.f += 8;
        }

		filter.f = Clamp(filter.f, 40.0, 5000.0);


		// Change filter Q

        if (Input::KeyDown.pressed)
        {
			filter.q -= 0.02;
        }

        if (Input::KeyUp.pressed)
        {
			filter.q += 0.02;
        }

		filter.q = Clamp(filter.q, 0.10, 10.00);


		// Apply filter to next block

        const int idx_block = ((pos + BufLen) % len) / BufLen;

		if ((Input::KeyLeft | Input::KeyRight | Input::KeyDown | Input::KeyUp).pressed)
		{
			filter.calc();
		}

		if ((Input::KeyLeft | Input::KeyRight | Input::KeyDown | Input::KeyUp).released)
		{
			block.reset();
			block[idx_block] = true;
		}

        if (!block[idx_block])
        {
			size_t start = idx_block * BufLen;
            applyFilter(sound, wave, waveOrig, start, BufLen, filter, out);
			block[idx_block] = true;
		}


		// Draw

		const double volL = Abs(wave[pos].left) / 32768.0;
		const double volR = Abs(wave[pos].right) / 32768.0;
		const int wsize = Min(Window::Width(), Window::Height()) / 2;

		// BG
        Rect(Window::Width() / 2, Window::Height()).draw(ColorF(volL));
        Rect(Window::Width() / 2, 0, Window::Width() / 2, Window::Height()).draw(ColorF(volR));

		// Volume circle
		Graphics2D::SetBlendState(BlendState::Additive);
		Circle(Window::Center(), wsize * 0.1 + wsize * 1.2 * volL).drawArc(180_deg, 180_deg, 10, 0, Color(50 + 100 * volL));
		Circle(Window::Center(), wsize * 0.1 + wsize * 1.2 * volR).drawArc(0_deg, 180_deg, 10, 0, Color(50 + 100 * volR));
		Graphics2D::SetBlendState(BlendState::Default);

		// Cutoff-freq. & Q
		fontHz(filter.f, L" Hz").drawCenter(Window::Center().movedBy(1, -19), Palette::Black);
		fontHz(filter.f, L" Hz").drawCenter(Window::Center().movedBy(0, -20), Palette::White);
		fontQ(L"Q=", filter.q).drawCenter(Window::Center().movedBy(1, 41), Palette::Gray);
		fontQ(L"Q=", filter.q).drawCenter(Window::Center().movedBy(0, 40), Palette::Black);


		// Position slider

		const int silderHeight = 24;
		Rect slider(Window::Width(), silderHeight);
		slider.draw(Color(0, 128));
		Rect(Window::Width() * pos / len, silderHeight).draw(Color(255 - 128 * (System::FrameCount() % 2), 128));


		// Position change

		if (slider.leftClicked)
		{
			block.reset();
			filter.calc();

			const size_t newpos = len * Mouse::Pos().x / Window::Width();
			const size_t idx_block = newpos / BufLen;
			const size_t start = idx_block * BufLen;
			applyFilter(sound, wave, waveOrig, start, BufLen, filter, out);
			block[idx_block] = true;

			sound.setPosSample(newpos);
		}


		// Drag-Drop audio file

		if (Dragdrop::HasItems())
		{
			sound.stop();
			wave = Wave(Dragdrop::GetFilePaths()[0]);
			waveOrig = wave.clone();
			sound = Sound(wave, SoundLoop::All);
			sound.setPosSample(0);
			len = sound.lengthSample();

			block = FilteredBlock(len, BufLen);
			block.reset();
			filter.calc();
			applyFilter(sound, wave, waveOrig, 0, BufLen, filter, out);
			block[0] = true;

			sound.play();
		}

		
		// FFT result

		const auto fft = FFT::Analyze(sound);

		for (int i : step(fft.length()))
		{
			const double s = Pow(fft.buffer[i], 0.6);

			Rect((double)i / fft.length() * Window::Width(), Window::Height() - s * Window::Height() * 1.2, Max((double)i / fft.length(), 1.0), s * Window::Height() * 1.2).draw(Color(255, 50 + s * 80));
		}
    }
}
