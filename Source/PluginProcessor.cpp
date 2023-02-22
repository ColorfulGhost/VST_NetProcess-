/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <Windows.h>
#include <fstream>
#include "AudioWork.h"
using namespace std::chrono;

//==============================================================================
NetProcessJUCEVersionAudioProcessor::NetProcessJUCEVersionAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
	loadConfig();
}

NetProcessJUCEVersionAudioProcessor::~NetProcessJUCEVersionAudioProcessor()
{
	loadConfig();
}

//==============================================================================
const juce::String NetProcessJUCEVersionAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NetProcessJUCEVersionAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NetProcessJUCEVersionAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NetProcessJUCEVersionAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NetProcessJUCEVersionAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NetProcessJUCEVersionAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int NetProcessJUCEVersionAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NetProcessJUCEVersionAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String NetProcessJUCEVersionAudioProcessor::getProgramName (int index)
{
    return {};
}

void NetProcessJUCEVersionAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void NetProcessJUCEVersionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
	// reset state and buffer
	iNumberOfChanel = 1;
	lNoOutputCount = 0;
	bDoItSignal = false;

	// ǰ�������������ʼ��
	// ׼��20s�Ļ�����
	lPrefixBufferSize = static_cast<long>(20.0f * sampleRate);
	// ǰ�����峤��
	lPrefixLengthSampleNumber = static_cast<long>(fPrefixLength * sampleRate);
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixBufferSize);
	lPrefixBufferPos = 0;

	lModelOutputBufferSize = static_cast<long>(fModelOutputBufferSecond * sampleRate);
	fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelOutputBufferSize));
	lModelOutputSampleBufferReadPos = 0;
	lastVoiceSampleCrossFadeSkipNumber = 0;
	lModelOutputSampleBufferWritePos = 0;

	modelInputJobList.clear();
	prepareModelInputJob.clear();

	// worker�̰߳�ȫ�˳�����ź�
	bWorkerNeedExit = false;
	kRecordState = IDLE;
	runWorker();
}

void NetProcessJUCEVersionAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
	bWorkerNeedExit = true;
	// �����̻߳�������ʱ������������ϵģ���ʱ���̻߳������˳�
	// ���߳�ͨ�����˳��źŷ������̣߳��ȴ����̰߳�ȫ�˳����ͷ��������߳����˳�
	mWorkerSafeExit.lock();
	mWorkerSafeExit.unlock();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NetProcessJUCEVersionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void NetProcessJUCEVersionAudioProcessor::processBlock (juce::AudioBuffer<float>& audioBuffer, juce::MidiBuffer& midiMessages)
{
	if (bConfigLoadFinished) {
		juce::ScopedNoDenormals noDenormals;
		auto totalNumInputChannels = getTotalNumInputChannels();
		auto totalNumOutputChannels = getTotalNumOutputChannels();

		// In case we have more outputs than inputs, this code clears any output
		// channels that didn't contain input data, (because these aren't
		// guaranteed to be empty - they may contain garbage).
		// This is here to avoid people getting screaming feedback
		// when they first compile a plugin, but obviously you don't need to keep
		// this code if your algorithm always overwrites all the output channels.
		for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
			audioBuffer.clear(i, 0, audioBuffer.getNumSamples());
		}

		double fSampleMax = -9999;
		float* inputOutputL = audioBuffer.getWritePointer(0);
		float* inputOutputR = NULL;
		bool bHasRightChanel = false;
		if (getNumOutputChannels() > 1) {
			inputOutputR = audioBuffer.getWritePointer(1);
			bHasRightChanel = true;
		}
		else {
			inputOutputR = inputOutputL;
		}
		for (juce::int32 i = 0; i < audioBuffer.getNumSamples(); i++) {
			// ��ȡ��ǰ����������
			float fCurrentSample = inputOutputL[i];
			float fSampleAbs = std::abs(fCurrentSample);
			if (fSampleAbs > fSampleMax) {
				fSampleMax = fSampleAbs;
			}
		}

		if (bRealTimeMode) {
			// ���������ʵʱģʽ����������������ֵ
			bVolumeDetectFine = true;
		}
		else {
			bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;
		}

		if (bVolumeDetectFine) {
			fRecordIdleTime = 0.f;
		}
		else {
			fRecordIdleTime += 1.f * audioBuffer.getNumSamples() / getSampleRate();
			/*if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "��ǰ�ۻ�����ʱ��:%f\n", fRecordIdleTime);
				OutputDebugStringA(buff);
			}*/
		}

		if (kRecordState == IDLE) {
			// ��ǰ�ǿ���״̬
			if (bVolumeDetectFine) {
				/*if (bEnableDebug) {
					OutputDebugStringA("�л�������״̬");
				}*/
				kRecordState = WORK;

				// ��IDLE�л���WORD����ʾ��Ҫһ���µ�ģ������
				if (bRealTimeMode && fPrefixLength > 0.01f) {
					// ʵʱģʽ
					// ��ǰ��������������д�뵽ģ����λ�������
					// ��ǰ����������ǰλ����ǰѰ��lPrefixLengthSampleNumber������ 
					int readPosStart = lPrefixBufferPos - lPrefixLengthSampleNumber;
					int readPosEnd = lPrefixBufferPos;
					if (readPosStart >= 0) {
						// ����1��[.....start......end...]��ֱ�Ӵ��м��ȡ��Ҫ������
						for (int i = readPosStart; i < readPosEnd; i++) {
							prepareModelInputJob.push_back(fPrefixBuffer[i]);
						}
					}
					else {
						// ����2��[.....end......start...]����Ҫ��ѭ��������β����ȡһЩ���ݣ�Ȼ���ٴ�ͷ����ȡһЩ����
						readPosStart = lPrefixBufferSize + readPosStart - 1;
						for (int i = readPosStart; i < lPrefixBufferSize; i++) {
							prepareModelInputJob.push_back(fPrefixBuffer[i]);
						}
						for (int i = 0; i < readPosEnd; i++) {
							prepareModelInputJob.push_back(fPrefixBuffer[i]);
						}
					};
				};
				
				// ����ǰ����Ƶ����д�뵽ģ�����뻺������
				for (int i = 0; i < audioBuffer.getNumSamples(); i++) {
					prepareModelInputJob.push_back(inputOutputL[i]);
				};
			}
		}
		else {
			// ��ǰ�ǹ���״̬
			// ֻ��Ҫ��������ǰ����Ƶ����д�뵽ģ����λ�������
			for (int i = 0; i < audioBuffer.getNumSamples(); i++) {
				prepareModelInputJob.push_back(inputOutputL[i]);
			}

			// �ж��Ƿ���Ҫ�˳�����״̬
			bool bExitWorkState = false;

			// �˳�����1��������С�ҳ�������һ��ʱ��
			if (fRecordIdleTime >= fLowVolumeDetectTime) {
				bExitWorkState = true;
				/*if (bEnableDebug) {
					OutputDebugStringA("������С�ҳ�������һ��ʱ�䣬ֱ�ӵ���ģ��\n");
				}*/
			}

			// �˳�����2�����дﵽһ���Ĵ�С
			//long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, lModelInputSampleBufferReadPos, lModelInputSampleBufferWritePos);
			long inputBufferSize = prepareModelInputJob.size();
			if (inputBufferSize > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
				bExitWorkState = true;
				/*if (bEnableDebug) {
					OutputDebugStringA("���д�С�ﵽԤ�ڣ�ֱ�ӵ���ģ��\n");
				}*/
			}

			if (bExitWorkState) {
				// ��Ҫ�˳�����״̬
				// 1.ģ��������м���
				// 2.����ǰ׼����ģ������������
				// 3.׼��һ���µ�ģ�����빩��һ��ʹ��
				// 4.���ñ��λ���������߼��
				// 5.ģ������������ͷ�
				//long long tStart = func_get_timestamp();
				modelInputJobListMutex.lock();
				modelInputJobList.push_back(prepareModelInputJob);
				std::vector<float> newPrepareModelInputJob;
				prepareModelInputJob = newPrepareModelInputJob;
				kRecordState = IDLE;
				bDoItSignal = true;
				modelInputJobListMutex.unlock();
				/*long long tUseTime = func_get_timestamp() - tStart;
				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "��Ƶ�߳�����ʱ:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}*/
			}
		}

		// ����ǰ����Ƶ����д�뵽ǰ���������У�����һ��ʹ��
		for (int i = 0; i < audioBuffer.getNumSamples(); i++) {
			fPrefixBuffer[lPrefixBufferPos++] = inputOutputL[i];
			if (lPrefixBufferPos == lPrefixBufferSize) {
				lPrefixBufferPos = 0;
			}
		}

		// ���ģ������������������ݵĻ���д�뵽����ź���ȥ
		int outputWritePos = 0;
		bool bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
		if (bHasMoreData) {
			for (int i = 0; i < audioBuffer.getNumSamples(); i++)
			{
				bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
				if (!bHasMoreData) {
					break;
				}
				double currentSample = fModeulOutputSampleBuffer[lModelOutputSampleBufferReadPos++];
				if (lModelOutputSampleBufferReadPos == lModelOutputBufferSize) {
					lModelOutputSampleBufferReadPos = 0;
				}
				inputOutputL[i] = static_cast<float>(currentSample);
				if (bHasRightChanel) {
					inputOutputR[i] = static_cast<float>(currentSample);
				};
				outputWritePos = i + 1;
			}
		}
		// �жϵ�ǰ�Ƿ��п����
		bool hasEmptyBlock = outputWritePos != audioBuffer.getNumSamples();
		if (hasEmptyBlock) {
			if (bRealTimeMode) {
				// ʵʱģʽ
				// �ӡ������浭�����ݡ�����ȡ��һ��������
				int blockRemainNeedSampleNumber = audioBuffer.getNumSamples() - outputWritePos;
				lastVoiceSampleForCrossFadeVectorMutex.lock();
				int peekDataSize = min(blockRemainNeedSampleNumber, lastVoiceSampleForCrossFadeVector.size());
				//peekDataSize = 0;
				lastVoiceSampleCrossFadeSkipNumber += peekDataSize;
 				if (peekDataSize > 0) {
					if (bEnableDebug) {
						snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!ʵʱģʽ-ʹ�ý��浭��������ǰ�������������:%ld\n", lNoOutputCount);
						OutputDebugStringA(buff);
					}
					for (int i = 0; i < peekDataSize; i++) {
						auto peekSample = lastVoiceSampleForCrossFadeVector.at(0);
						lastVoiceSampleForCrossFadeVector.erase(lastVoiceSampleForCrossFadeVector.begin());
						inputOutputL[outputWritePos] = static_cast<float>(peekSample);
						if (bHasRightChanel) {
							inputOutputR[outputWritePos] = static_cast<float>(peekSample);
						};
						outputWritePos++;
					}
				};
				lastVoiceSampleForCrossFadeVectorMutex.unlock();
				hasEmptyBlock = outputWritePos != audioBuffer.getNumSamples();
				if (hasEmptyBlock) {
					// ���п��������ʱ�Ѿ�û�����ݿ�������
					// �������
					lNoOutputCount += 1;
					if (bEnableDebug) {
						snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!ʵʱģʽ-��������:%ld\n", lNoOutputCount);
						OutputDebugStringA(buff);
					}
					for (juce::int32 i = outputWritePos; i < audioBuffer.getNumSamples(); i++) {
						// ���������
						inputOutputL[i] = 0.0000000001f;
						if (bHasRightChanel) {
							inputOutputR[i] = 0.0000000001f;
						}
					}
				}
			}
			else {
				// �־�ģʽ
				// �����ݿ���ȡ�ˣ��������
				lNoOutputCount += 1;
				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!�־�ģʽ-��������:%ld\n", lNoOutputCount);
					OutputDebugStringA(buff);
				}
				for (juce::int32 i = outputWritePos; i < audioBuffer.getNumSamples(); i++) {
					// ���������
					inputOutputL[i] = 0.0000000001f;
					if (bHasRightChanel) {
						inputOutputR[i] = 0.0000000001f;
					}
				}
			}
		}
	}
}

//==============================================================================
bool NetProcessJUCEVersionAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* NetProcessJUCEVersionAudioProcessor::createEditor()
{
    return new NetProcessJUCEVersionAudioProcessorEditor (*this);
}

//==============================================================================
void NetProcessJUCEVersionAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
	std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("Config"));
	xml->setAttribute("fMaxSliceLength", (double)fMaxSliceLength);
	xml->setAttribute("fMaxSliceLengthForRealTimeMode", (double)fMaxSliceLengthForRealTimeMode);
	xml->setAttribute("fMaxSliceLengthForSentenceMode", (double)fMaxSliceLengthForSentenceMode);
	xml->setAttribute("fLowVolumeDetectTime", (double)fLowVolumeDetectTime);
	xml->setAttribute("fPrefixLength", (double)fPrefixLength);
	xml->setAttribute("fDropSuffixLength", (double)fDropSuffixLength);
	xml->setAttribute("fPitchChange", (double)fPitchChange);
	xml->setAttribute("bRealTimeMode", bRealTimeMode);
	xml->setAttribute("bEnableDebug", bEnableDebug);
	xml->setAttribute("iSelectRoleIndex", iSelectRoleIndex);
	copyXmlToBinary(*xml, destData);
}

void NetProcessJUCEVersionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{	
	// load last state
	std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

	if (xmlState.get() != nullptr)
		if (xmlState->hasTagName("Config")) {
			fMaxSliceLength = (float)xmlState->getDoubleAttribute("fMaxSliceLength", 1.0);
			fMaxSliceLengthForRealTimeMode = (float)xmlState->getDoubleAttribute("fMaxSliceLengthForRealTimeMode", 1.0);
			fMaxSliceLengthForSentenceMode = (float)xmlState->getDoubleAttribute("fMaxSliceLengthForSentenceMode", 1.0);
			lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
			fLowVolumeDetectTime = (float)xmlState->getDoubleAttribute("fLowVolumeDetectTime", 0.4);
			fPrefixLength = (float)xmlState->getDoubleAttribute("fPrefixLength", 0.0);
			fDropSuffixLength = (float)xmlState->getDoubleAttribute("fDropSuffixLength", 0.0);
			fPitchChange = (float)xmlState->getDoubleAttribute("fPitchChange", 1.0);
			bRealTimeMode = (bool)xmlState->getBoolAttribute("bRealTimeMode", false);
			bEnableDebug = (bool)xmlState->getBoolAttribute("bEnableDebug", false);
			iSelectRoleIndex = (int)xmlState->getIntAttribute("iSelectRoleIndex", 0);
		}
}

void NetProcessJUCEVersionAudioProcessor::loadConfig()
{
	std::wstring sDllPath = L"C:/Program Files/Common Files/VST3/NetProcessJUCEVersion/samplerate.dll";
	std::string sJsonConfigFileName = "C:/Program Files/Common Files/VST3/NetProcessJUCEVersion/netProcessConfig.json";
	
	if (!bConfigLoadFinished) {
		// default value
		fMaxSliceLength = 5.0f;
		fMaxSliceLengthForRealTimeMode = fMaxSliceLength;
		fMaxSliceLengthForSentenceMode = fMaxSliceLength;
		fLowVolumeDetectTime = 0.4f;
		fPrefixLength = 0.0f;
		fDropSuffixLength = 0.0f;
		lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
		fPitchChange = 0.0f;
		bRealTimeMode = false;
		bEnableDebug = false;
		iSelectRoleIndex = 0;

		auto dllClient = LoadLibraryW(sDllPath.c_str());
		if (dllClient != NULL) {
			dllFuncSrcSimple = (FUNC_SRC_SIMPLE)GetProcAddress(dllClient, "src_simple");
		}
		else {
			OutputDebugStringA("samplerate.dll load Error!");
		}

		// ��ȡJSON�����ļ�
		std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
		std::stringstream buffer_pc_file;
		buffer_pc_file << t_pc_file.rdbuf();

		juce::var jsonVar;
		if (juce::JSON::parse(buffer_pc_file.str(), jsonVar).wasOk()) {
			auto& props = jsonVar.getDynamicObject()->getProperties();
			bEnableSOVITSPreResample = props["bEnableSOVITSPreResample"];
			iSOVITSModelInputSamplerate = props["iSOVITSModelInputSamplerate"];
			bEnableHUBERTPreResample = props["bEnableHUBERTPreResample"];
			iHUBERTInputSampleRate = props["iHUBERTInputSampleRate"];
			fSampleVolumeWorkActiveVal = props["fSampleVolumeWorkActiveVal"];

			roleList.clear();
			auto jsonRoleList = props["roleList"];
			int iRoleSize = jsonRoleList.size();
			for (int i = 0; i < iRoleSize; i++) {
				auto& roleListI = jsonRoleList[i].getDynamicObject()->getProperties();
				std::string apiUrl = roleListI["apiUrl"].toString().toStdString();
				std::string name = roleListI["name"].toString().toStdString();
				std::string speakId = roleListI["speakId"].toString().toStdString();
				roleStruct role;
				role.sSpeakId = speakId;
				role.sName = name;
				role.sApiUrl = apiUrl;
				roleList.push_back(role);
			}
			if (iSelectRoleIndex + 1 > iRoleSize || iSelectRoleIndex < 0) {
				iSelectRoleIndex = 0;
			}
		}
		else {
			// error read json
		};

		bConfigLoadFinished = true;
	}
}

void NetProcessJUCEVersionAudioProcessor::runWorker()
{
    // ����Worker�߳�
    std::thread(func_do_voice_transfer_worker,
        iNumberOfChanel,					// ͨ������
        getSampleRate(),		            // ��Ŀ������

		&modelInputJobList,					// ģ���������
		&modelInputJobListMutex,			// ģ�����������

		lModelOutputBufferSize,				// ģ�������������С
        fModeulOutputSampleBuffer,			// ģ�����������
        &lModelOutputSampleBufferReadPos,	// ģ�������������ָ��
        &lModelOutputSampleBufferWritePos,	// ģ�����������дָ��

		&lastVoiceSampleForCrossFadeVectorMutex,
		&lastVoiceSampleForCrossFadeVector, //���һ��ģ�������Ƶ��β�������ڽ��浭������
		&lastVoiceSampleCrossFadeSkipNumber,

		&fPrefixLength,						// ǰ��������ʱ��(s)
		&fDropSuffixLength,					// ������β��ʱ��(s)
        &fPitchChange,						// �����仯��ֵ
        &bCalcPitchError,					// �������������

        roleList,							// ���õĿ�����ɫ�б�
        &iSelectRoleIndex,					// ѡ��Ľ�ɫID
        dllFuncSrcSimple,					// DLL�ڲ�SrcSimple����

        &bEnableSOVITSPreResample,			// ����SOVITSģ�������Ƶ�ز���Ԥ����
        iSOVITSModelInputSamplerate,		// SOVITSģ����β�����
        &bEnableHUBERTPreResample,			// ����HUBERTģ�������Ƶ�ز���Ԥ����
        iHUBERTInputSampleRate,				// HUBERTģ����β�����

        &bRealTimeMode,					    // ռλ����ʵʱģʽ
        &bDoItSignal,						// ռλ������ʾ��worker�д����������
		&bEnableDebug,						// ռλ��������DEBUG���
		vServerUseTime,						// UI��������ʾ������ú�ʱ
		vDropDataLength,					// UI��������ʾʵʱģʽ�¶�������Ƶ���ݳ���

        &bWorkerNeedExit,					// ռλ������ʾworker�߳���Ҫ�˳�
        &mWorkerSafeExit					// ����������ʾworker�߳��Ѿ���ȫ�˳�
    ).detach();


}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NetProcessJUCEVersionAudioProcessor();
}
