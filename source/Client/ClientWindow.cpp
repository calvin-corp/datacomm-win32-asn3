#include "ClientWindow.h"

#include "resource.h"

#include "../GuiLibrary/GuiPanel.h"
#include "../GuiLibrary/GuiStatusBar.h"
#include "../GuiLibrary/GuiLinearLayout.h"
#include "../GuiLibrary/GuiButton.h"
#include "../GuiLibrary/GuiLabel.h"
#include "../GuiLibrary/GuiScrollList.h"
#include "../GuiLibrary/GuiScrollBar.h"
#include "../GuiLibrary/GuiTextBox.h"
#include "PlaybackTrackerPanel.h"
#include "ButtonPanel.h"
#include "FileListItem.h"
#include "ConnectionWindow.h"
#include "../Buffer/MessageQueue.h"
#include "../Buffer/JitterBuffer.h"
#include "ReceiveThread.h"
#include "VoiceBufferer.h"
#include "MicReader.h"
#include "PlayWave.h"
#include "MusicBufferer.h"
#include "MusicReader.h"
#include "MusicBuffer.h"

ClientWindow::ClientWindow(HINSTANCE hInst)
	: GuiWindow(hInst)
{
	setup(windowClass, NULL, windowStyles);
	setMiniumSize(700, 325);
	setExitOnClose(true);

	playButtonUp = LoadBitmap(hInst, L"IMG_PLAY_BUTTON_UP");
	playButtonDown = LoadBitmap(hInst, L"IMG_PLAY_BUTTON_DOWN");

	stopButtonUp = LoadBitmap(hInst, L"IMG_STOP_BUTTON_UP");
	stopButtonDown = LoadBitmap(hInst, L"IMG_STOP_BUTTON_DOWN");

	darkBackground = (HBRUSH)CreateSolidBrush(RGB(15, 15, 15));
	lightBackground = (HBRUSH)CreateSolidBrush(RGB(30, 30, 30));;
	accentBrush = (HBRUSH)CreateSolidBrush(RGB(0, 162, 232));
	nullPen = (HPEN)CreatePen(PS_SOLID, 0, 0);
	borderPen = (HPEN)CreatePen(PS_SOLID, 1, RGB(128, 0, 128));

	recording = false;
	requestingRecorderStop = false;
	micMQueue = new MessageQueue(1000,AUDIO_BUFFER_LENGTH);

	MessageQueue* q1 = new MessageQueue(1500,sizeof(LocalDataPacket));
	JitterBuffer* musicJitBuf = new JitterBuffer(5000,100,AUDIO_BUFFER_LENGTH,50,0);
	udpSock = new UDPSocket(MULTICAST_PORT,q1);
	ReceiveThread* recvThread = new ReceiveThread(musicJitBuf,q1);

	udpSock->setGroup(MULTICAST_ADDR,1);
	recvThread->start();

	MusicBuffer* musicfile = new MusicBuffer();
	MessageQueue* q2 = new MessageQueue(1500,AUDIO_BUFFER_LENGTH);
	MusicBufferer* musicbuf = new MusicBufferer(musicJitBuf, musicfile);
	MusicReader* mreader = new MusicReader(q2, musicfile);
	PlayWave* playWave = new PlayWave(1000, q2);

	playWave->startPlaying(AUDIO_SAMPLE_RATE, AUDIO_BITS_PER_SAMPLE, NUM_AUDIO_CHANNELS);

	HANDLE ThreadHandle;
	DWORD ThreadId;

	if ((ThreadHandle = CreateThread(NULL, 0, MicThread, (void*)this, 0, &ThreadId)) == NULL)
	{
		MessageBox(NULL, L"CreateThread failed with error", L"ERROR", MB_ICONERROR);
		return;
	}
}

DWORD WINAPI ClientWindow::MicThread(LPVOID lpParameter)
{
	ClientWindow* This = (ClientWindow*)lpParameter;
	return This->ThreadStart();
}

DWORD ClientWindow::ThreadStart(void)
{
	int useless;
	int length;

	voicePacket.index = 0;

	// continuously send voice data over the network when it becomes available
	while (true)
	{
		++(voicePacket.index);
		micMQueue->dequeue(&useless, voicePacket.data, &length);
		udpSock->sendtoGroup(MICSTREAM, &voicePacket, sizeof(DataPacket));
	}
}

ClientWindow::~ClientWindow()
{
	delete topPanel;
	delete topPanelStretch;
	delete fileContainerPanel;
	delete seekPanel;
	delete micTargetButton;
	delete statusBar;
	delete trackerPanel;
	delete buttonSpacer1;
	delete buttonSpacer2;
	delete bottomSpacer;
	delete playButton;
	delete stopButton;
	delete voiceTargetInput;
	delete voiceTargetLabel;

	DeleteObject(playButtonUp);
	DeleteObject(playButtonDown);
	DeleteObject(stopButtonUp);
	DeleteObject(stopButtonDown);
	DeleteObject(nullPen);
	DeleteObject(darkBackground);
	DeleteObject(lightBackground);
	DeleteObject(accentBrush);
	DeleteObject(borderPen);
}

void ClientWindow::addRemoteFile(LPWSTR filename)
{
	fileContainerPanel->addItem(new FileListItem(fileContainerPanel, this, hInst, filename));
}

void ClientWindow::onCreate()
{
	setTitle(L"CommAudio Client");
	setSize(700, 325);
	micReader = new MicReader(AUDIO_SAMPLE_RATE, AUDIO_BUFFER_LENGTH, micMQueue, getHWND());
	this->addMessageListener(WM_MIC_STOPPED_READING, onMicStop, this);

	// Create Elements
	topPanel = new GuiPanel(hInst, this);
	topPanelStretch = new GuiPanel(hInst, topPanel);
	fileContainerPanel = new GuiScrollList(hInst, this);
	seekPanel = new GuiPanel(hInst, this);
	voiceTargetLabel = new GuiLabel(hInst, topPanel);
	voiceTargetInput = new GuiTextBox(hInst, topPanel, false);
	micTargetButton = new GuiButton(hInst, topPanel, IDB_MIC_TOGGLE);
	statusBar = new GuiStatusBar(hInst, this);
	trackerPanel = new PlaybackTrackerPanel(hInst, this);
	playButton = new ButtonPanel(hInst, seekPanel, playButtonUp, playButtonDown);
	stopButton = new ButtonPanel(hInst, seekPanel, stopButtonUp, stopButtonDown);
	buttonSpacer1 = new GuiPanel(hInst, seekPanel);
	buttonSpacer2 = new GuiPanel(hInst, seekPanel);
	bottomSpacer = new GuiPanel(hInst, this);

	// Create Layouts
	GuiLinearLayout *layout = (GuiLinearLayout*) this->getLayoutManager();
	GuiLinearLayout::Properties layoutProps;
	layout->zeroProperties(&layoutProps);
	layout->setHorizontal(false);

	// Add Top Panel to Layout
	topPanel->init();
	topPanel->enableCustomDrawing(true);
	topPanel->setBorderPen(nullPen);
	topPanel->setBackgroundBrush(darkBackground);
	topPanel->setPreferredSize(0, 32);
	layout->addComponent(topPanel);

	// Add File Panel to layout
	fileContainerPanel->init();
	fileContainerPanel->enableCustomDrawing(true);
	fileContainerPanel->setBorderPen(nullPen);
	fileContainerPanel->setBackgroundBrush(lightBackground);
	fileContainerPanel->getScrollBar()->setColours(NULL, NULL, NULL, accentBrush);
	layoutProps.weight = 1;
	fileContainerPanel->setPreferredSize(0, 0);
	layout->addComponent(fileContainerPanel, &layoutProps);

	// Add some files to the list view
	addRemoteFile(L"This_Was_A_TRIUMPH_DUH_NUH_NUH_NUH_DUH.wav");
	addRemoteFile(L"This_Was_A_TRIUMPH_DUH_NUH_NUH_NUH_DUH_1.wav");
	addRemoteFile(L"This_Was_A_TRIUMPH_DUH_NUH_NUH_NUH_DUH_2.wav");
	addRemoteFile(L"This_Was_A_TRIUMPH_DUH_NUH_NUH_NUH_DUH_3.wav");
	addRemoteFile(L"This_Was_A_TRIUMPH_DUH_NUH_NUH_NUH_DUH_4.wav");

	// Add Tracker Panel to layout
	trackerPanel->init();
	trackerPanel->setPreferredSize(0, 24);
	trackerPanel->setPercentageBuffered(.75);
	trackerPanel->setTrackerPercentage(.25);
	trackerPanel->setBorderPen(nullPen);
	trackerPanel->addMessageListener(WM_SEEK, ClientWindow::onSeek, this);
	layout->addComponent(trackerPanel, &layoutProps);

	// Add Control Panel to layout
	seekPanel->init();
	seekPanel->setPreferredSize(0, 64);
	seekPanel->enableCustomDrawing(true);
	seekPanel->setBorderPen(nullPen);
	seekPanel->setBackgroundBrush(darkBackground);
	layout->addComponent(seekPanel);

	// Add bottom spacer
	bottomSpacer->init();
	bottomSpacer->setPreferredSize(0, 8);
	bottomSpacer->enableCustomDrawing(true);
	bottomSpacer->setBorderPen(nullPen);
	bottomSpacer->setBackgroundBrush(darkBackground);
	layout->addComponent(bottomSpacer);

	// Add Top Spacer
	layout = (GuiLinearLayout*)topPanel->getLayoutManager();
	layout->setHorizontal(true);
	topPanelStretch->init();
	topPanelStretch->setPreferredSize(150, 0);
	layout->addComponent(topPanelStretch);

	// Add Microphone Label
	voiceTargetLabel->init();
	voiceTargetLabel->setText(L"Target");
	layout->addComponent(voiceTargetLabel);

	// Add Microphone Textbox
	voiceTargetInput->init();
	voiceTargetInput->enableCustomDrawing(false);
	voiceTargetInput->setPreferredSize(184, 32);
	layout->addComponent(voiceTargetInput);

	// Add Microphone Button
	micTargetButton->init();
	micTargetButton->setText(L"Start Speaking");
	layout->addComponent(micTargetButton);
	topPanel->addCommandListener(BN_CLICKED, onClickMic, this);

	// Create Play Button
	playButton->init();
	playButton->setClickListener(ClientWindow::onClickPlay);
	playButton->setPreferredSize(64, 64);
	playButton->enableCustomDrawing(true);
	playButton->setBackgroundBrush(darkBackground);

	// Create Stop Button
	stopButton->init();
	stopButton->setClickListener(ClientWindow::onClickStop);
	stopButton->setPreferredSize(64, 64);
	stopButton->enableCustomDrawing(true);
	stopButton->setBackgroundBrush(darkBackground);

	// Create Button Spacers
	buttonSpacer1->init();
	buttonSpacer1->enableCustomDrawing(true);
	buttonSpacer1->setBorderPen(nullPen);
	buttonSpacer1->setBackgroundBrush(darkBackground);

	buttonSpacer2->init();
	buttonSpacer2->enableCustomDrawing(true);
	buttonSpacer2->setBorderPen(nullPen);
	buttonSpacer2->setBackgroundBrush(darkBackground);

	// Add Play Button In Center
	layout = (GuiLinearLayout*)seekPanel->getLayoutManager();
	layout->setHorizontal(true);

	layout->addComponent(buttonSpacer1);
	layout->addComponent(playButton);
	layout->addComponent(stopButton);
	layout->addComponent(buttonSpacer2);
}

void ClientWindow::onClickPlay(void*)
{
	MessageBox(NULL, L"CLICKED PLAY!", L"YAY!", MB_ICONINFORMATION);
}

void ClientWindow::onClickStop(void*)
{
	MessageBox(NULL, L"CLICKED STOP!", L"BOO!", MB_ICONINFORMATION);
}

bool ClientWindow::onClickMic(GuiComponent *_pThis, UINT command, UINT id, WPARAM wParam, LPARAM lParam, INT_PTR *retval)
{
	ClientWindow *pThis = (ClientWindow*)_pThis;

	if (!pThis->requestingRecorderStop)
	{
		if (pThis->recording)
		{
			pThis->requestingRecorderStop = true;
			pThis->micReader->stopReading();
		}
		else
		{
			pThis->micTargetButton->setText(L"Stop Speaking");
			pThis->recording = true;
			pThis->micReader->startReading();
		}
	}

	return true;
}

bool ClientWindow::onMicStop(GuiComponent *_pThis, UINT command, UINT id, WPARAM wParam, LPARAM lParam, INT_PTR *retval)
{
	ClientWindow *pThis = (ClientWindow*)_pThis;

	pThis->micTargetButton->setText(L"Start Speaking");
	pThis->recording = false;
	pThis->requestingRecorderStop = false;
    pThis->voicePacket.index = 0;

	return true;
}

bool ClientWindow::onSeek(GuiComponent *_pThis, UINT command, UINT id, WPARAM wParam, LPARAM lParam, INT_PTR *retval)
{
	ClientWindow *pThis = (ClientWindow*)_pThis;
	MessageBox(NULL, L"LOL", L"LOLOLOLOLOLOLOLOLOLOL", MB_ICONEXCLAMATION);

	double percent = ((double)wParam) / 1000.0;

	/*
		Manuel, do stuff here.
	*/
	
	return true;
}
