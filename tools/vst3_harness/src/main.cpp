#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace
{
struct ParsedOptions
{
    juce::StringPairArray values;
    juce::StringArray flags;
    juce::String error;

    bool hasFlag (const juce::String& name) const
    {
        return flags.contains (name);
    }

    std::optional<juce::String> getValue (const juce::String& name) const
    {
        const auto value = values[name];
        if (value.isNotEmpty())
            return value;

        return std::nullopt;
    }
};

struct LoadedWave
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
};

struct ParameterOverride
{
    int index = -1;
    float normalised = 0.0f;
};

void printUsage()
{
    std::cout
        << "vst3_harness commands:\n"
        << "  --help\n"
        << "  dump-params --plugin <path/to/plugin.vst3>\n"
        << "  render --plugin <plugin.vst3> --in <dry.wav> --outdir <dir> --sr <sampleRate> --bs <blockSize> --ch <channels> [--warmup <blocks>] [--set-params \"index=value,...\"]\n"
        << "  analyze --dry <dry.wav> --wet <wet.wav> --outdir <dir> [--auto-align] [--null]\n";
}

juce::File resolvePath (const juce::String& path)
{
    juce::File file (path);
    if (juce::File::isAbsolutePath (path))
        return file;

    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

ParsedOptions parseOptions (const juce::StringArray& args, int startIndex)
{
    ParsedOptions parsed;

    for (int i = startIndex; i < args.size(); ++i)
    {
        const auto token = args[i];

        if (! token.startsWith ("--"))
        {
            parsed.error = "Unexpected argument: " + token;
            return parsed;
        }

        if (i + 1 < args.size() && ! args[i + 1].startsWith ("--"))
        {
            parsed.values.set (token, args[i + 1]);
            ++i;
        }
        else
        {
            parsed.flags.add (token);
        }
    }

    return parsed;
}

bool parseIntOption (const ParsedOptions& options, const juce::String& name, int& valueOut, juce::String& error)
{
    const auto value = options.getValue (name);
    if (! value.has_value())
    {
        error = "Missing required option: " + name;
        return false;
    }

    const auto text = value->trim();
    const auto parsed = text.getIntValue();

    if (text != juce::String (parsed))
    {
        error = "Invalid integer for " + name + ": " + text;
        return false;
    }

    valueOut = parsed;
    return true;
}

bool parseDoubleOption (const ParsedOptions& options, const juce::String& name, double& valueOut, juce::String& error)
{
    const auto value = options.getValue (name);
    if (! value.has_value())
    {
        error = "Missing required option: " + name;
        return false;
    }

    const auto text = value->trim();
    valueOut = text.getDoubleValue();

    if (! std::isfinite (valueOut) || valueOut <= 0.0)
    {
        error = "Invalid positive number for " + name + ": " + text;
        return false;
    }

    return true;
}

bool parseFileOption (const ParsedOptions& options, const juce::String& name, juce::File& fileOut, juce::String& error)
{
    const auto value = options.getValue (name);
    if (! value.has_value())
    {
        error = "Missing required option: " + name;
        return false;
    }

    fileOut = resolvePath (*value);
    return true;
}

bool parseParameterOverrides (const juce::String& text, std::vector<ParameterOverride>& overrides, juce::String& error)
{
    overrides.clear();

    const auto pairs = juce::StringArray::fromTokens (text, ",", "\"'");
    for (const auto& pairTextRaw : pairs)
    {
        const auto pairText = pairTextRaw.trim();
        if (pairText.isEmpty())
            continue;

        const auto eqIndex = pairText.indexOfChar ('=');
        if (eqIndex <= 0 || eqIndex >= pairText.length() - 1)
        {
            error = "Invalid --set-params token: " + pairText + " (expected index=value)";
            return false;
        }

        const auto indexText = pairText.substring (0, eqIndex).trim();
        const auto valueText = pairText.substring (eqIndex + 1).trim();

        const auto index = indexText.getIntValue();
        if (indexText != juce::String (index) || index < 0)
        {
            error = "Invalid parameter index in --set-params: " + indexText;
            return false;
        }

        const auto value = static_cast<float> (valueText.getDoubleValue());
        if (! std::isfinite (value) || value < 0.0f || value > 1.0f)
        {
            error = "Invalid normalised value in --set-params: " + valueText + " (expected 0..1)";
            return false;
        }

        overrides.push_back ({ index, value });
    }

    return true;
}

bool loadWaveFile (const juce::File& file, LoadedWave& loaded, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Audio file not found: " + file.getFullPathName();
        return false;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Failed to open WAV file: " + file.getFullPathName();
        return false;
    }

    const auto numChannels = juce::jmax (1, static_cast<int> (reader->numChannels));
    const auto numSamples = static_cast<int> (reader->lengthInSamples);

    loaded.buffer.setSize (numChannels, juce::jmax (0, numSamples));

    if (numSamples > 0)
        reader->read (&loaded.buffer, 0, numSamples, 0, true, true);

    loaded.sampleRate = reader->sampleRate;
    return true;
}

bool writeWaveFile (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate, juce::String& error)
{
    if (! file.getParentDirectory().createDirectory())
    {
        error = "Failed to create output directory: " + file.getParentDirectory().getFullPathName();
        return false;
    }

    auto stream = file.createOutputStream();
    if (stream == nullptr)
    {
        error = "Failed to open output file: " + file.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), sampleRate, static_cast<unsigned int> (buffer.getNumChannels()), 24, {}, 0));

    if (writer == nullptr)
    {
        error = "Failed to create WAV writer for: " + file.getFullPathName();
        return false;
    }

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
    {
        error = "Failed writing WAV file: " + file.getFullPathName();
        return false;
    }

    return true;
}

std::optional<juce::PluginDescription> findPluginDescription (const juce::File& pluginPath, juce::String& error)
{
    if (! pluginPath.exists())
    {
        error = "Plugin path not found: " + pluginPath.getFullPathName();
        return std::nullopt;
    }

    juce::AudioPluginFormatManager formatManager;
    juce::addHeadlessDefaultFormatsToManager (formatManager);

    juce::AudioPluginFormat* vst3Format = nullptr;
    for (auto* format : formatManager.getFormats())
    {
        if (format != nullptr && format->getName().containsIgnoreCase ("VST3"))
        {
            vst3Format = format;
            break;
        }
    }

    if (vst3Format == nullptr)
    {
        error = "VST3 format is not available in this build.";
        return std::nullopt;
    }

    juce::OwnedArray<juce::PluginDescription> found;
    vst3Format->findAllTypesForFile (found, pluginPath.getFullPathName());

    if (found.isEmpty())
    {
        error = "No plugin descriptions found in: " + pluginPath.getFullPathName();
        return std::nullopt;
    }

    auto description = *found[0];
    description.fileOrIdentifier = pluginPath.getFullPathName();
    return description;
}

std::unique_ptr<juce::AudioPluginInstance> createPluginInstance (const juce::PluginDescription& description,
                                                                 double sampleRate,
                                                                 int blockSize,
                                                                 juce::String& error)
{
    juce::AudioPluginFormatManager formatManager;
    juce::addHeadlessDefaultFormatsToManager (formatManager);
    return formatManager.createPluginInstance (description, sampleRate, blockSize, error);
}

bool configurePlugin (juce::AudioPluginInstance& plugin, int channels, double sampleRate, int blockSize)
{
    auto layout = plugin.getBusesLayout();
    const auto channelSet = channels == 1 ? juce::AudioChannelSet::mono()
                                          : juce::AudioChannelSet::discreteChannels (channels);

    if (layout.inputBuses.size() > 0)
        layout.inputBuses.set (0, channelSet);

    if (layout.outputBuses.size() > 0)
        layout.outputBuses.set (0, channelSet);

    plugin.setBusesLayout (layout);
    plugin.setPlayConfigDetails (channels, channels, sampleRate, blockSize);
    plugin.setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin.setNonRealtime (true);
    plugin.prepareToPlay (sampleRate, blockSize);
    plugin.reset();
    return true;
}

bool applyParameterOverrides (juce::AudioPluginInstance& plugin,
                              const std::vector<ParameterOverride>& overrides,
                              juce::String& error)
{
    const auto parameters = plugin.getParameters();

    for (const auto& override : overrides)
    {
        if (! juce::isPositiveAndBelow (override.index, parameters.size()))
        {
            error = "Parameter index out of range: " + juce::String (override.index);
            return false;
        }

        if (auto* parameter = parameters[override.index])
            parameter->setValueNotifyingHost (override.normalised);
    }

    return true;
}

void copyWithChannelMatch (const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& destination)
{
    destination.clear();

    if (source.getNumChannels() <= 0 || source.getNumSamples() <= 0 || destination.getNumSamples() <= 0)
        return;

    const auto samplesToCopy = juce::jmin (source.getNumSamples(), destination.getNumSamples());

    for (int ch = 0; ch < destination.getNumChannels(); ++ch)
    {
        const auto sourceChannel = juce::jmin (ch, source.getNumChannels() - 1);
        destination.copyFrom (ch, 0, source, sourceChannel, 0, samplesToCopy);
    }
}

bool hasNaNOrInf (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return true;
    }

    return false;
}

std::vector<float> makeMonoSignal (const juce::AudioBuffer<float>& buffer, int channelsToUse)
{
    std::vector<float> mono (static_cast<size_t> (buffer.getNumSamples()), 0.0f);

    if (channelsToUse <= 0 || buffer.getNumSamples() <= 0)
        return mono;

    const auto gain = 1.0f / static_cast<float> (channelsToUse);

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            mono[static_cast<size_t> (i)] += data[i] * gain;
    }

    return mono;
}

int findBestLag (const std::vector<float>& dry, const std::vector<float>& wet, int maxLag)
{
    auto bestLag = 0;
    auto bestCorr = -std::numeric_limits<double>::infinity();

    const auto drySize = static_cast<int> (dry.size());
    const auto wetSize = static_cast<int> (wet.size());

    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        const auto dryStart = juce::jmax (0, -lag);
        const auto wetStart = juce::jmax (0, lag);
        const auto count = juce::jmin (drySize - dryStart, wetSize - wetStart);

        if (count <= 32)
            continue;

        double corr = 0.0;
        double dryEnergy = 0.0;
        double wetEnergy = 0.0;

        for (int i = 0; i < count; ++i)
        {
            const auto d = static_cast<double> (dry[static_cast<size_t> (dryStart + i)]);
            const auto w = static_cast<double> (wet[static_cast<size_t> (wetStart + i)]);
            corr += d * w;
            dryEnergy += d * d;
            wetEnergy += w * w;
        }

        if (dryEnergy <= 1.0e-15 || wetEnergy <= 1.0e-15)
            continue;

        const auto normCorr = corr / std::sqrt (dryEnergy * wetEnergy);
        if (normCorr > bestCorr)
        {
            bestCorr = normCorr;
            bestLag = lag;
        }
    }

    return bestLag;
}

float computeRmsDb (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples, int channelsToUse)
{
    double sumSquares = 0.0;
    int count = 0;

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        const auto* data = buffer.getReadPointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s = static_cast<double> (data[i]);
            sumSquares += s * s;
        }

        count += numSamples;
    }

    if (count <= 0)
        return -300.0f;

    const auto rms = std::sqrt (sumSquares / static_cast<double> (count));
    return juce::Decibels::gainToDecibels (static_cast<float> (rms), -300.0f);
}

float computePeakDb (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples, int channelsToUse)
{
    auto peak = 0.0f;

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        const auto* data = buffer.getReadPointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (data[i]));
    }

    return juce::Decibels::gainToDecibels (peak, -300.0f);
}

bool writeMetricsJson (const juce::File& outputFile,
                       double sampleRate,
                       int channels,
                       int drySamples,
                       int wetSamples,
                       int overlapSamples,
                       int lagSamples,
                       bool autoAligned,
                       bool nullRequested,
                       float rmsDryDb,
                       float rmsWetDb,
                       float rmsDeltaDb,
                       float peakDeltaDb,
                       bool hasInvalidValues,
                       juce::String& error)
{
    juce::var root (new juce::DynamicObject());
    auto* object = root.getDynamicObject();

    object->setProperty ("sample_rate", sampleRate);
    object->setProperty ("channels", channels);
    object->setProperty ("dry_samples", drySamples);
    object->setProperty ("wet_samples", wetSamples);
    object->setProperty ("overlap_samples", overlapSamples);
    object->setProperty ("lag_samples", lagSamples);
    object->setProperty ("auto_align", autoAligned);
    object->setProperty ("null_requested", nullRequested);
    object->setProperty ("rms_dry_db", rmsDryDb);
    object->setProperty ("rms_wet_db", rmsWetDb);
    object->setProperty ("rms_delta_db", rmsDeltaDb);
    object->setProperty ("peak_delta_db", peakDeltaDb);
    object->setProperty ("nan_or_inf", hasInvalidValues);

    if (! outputFile.replaceWithText (juce::JSON::toString (root, true)))
    {
        error = "Failed to write metrics JSON: " + outputFile.getFullPathName();
        return false;
    }

    return true;
}

int runDumpParams (const ParsedOptions& options)
{
    juce::String error;
    juce::File pluginFile;

    if (! parseFileOption (options, "--plugin", pluginFile, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    const auto description = findPluginDescription (pluginFile, error);
    if (! description.has_value())
    {
        std::cerr << error << std::endl;
        return 1;
    }

    auto plugin = createPluginInstance (*description, 48000.0, 256, error);
    if (plugin == nullptr)
    {
        std::cerr << "Failed to instantiate plugin: " << error << std::endl;
        return 1;
    }

    const auto parameters = plugin->getParameters();
    for (int i = 0; i < parameters.size(); ++i)
    {
        const auto* parameter = parameters[i];
        std::cout << i << "\t"
                  << parameter->getName (128) << "\t"
                  << juce::String (parameter->getDefaultValue(), 6)
                  << std::endl;
    }

    return 0;
}

int runRender (const ParsedOptions& options)
{
    juce::String error;
    juce::File pluginFile;
    juce::File inputFile;
    juce::File outputDir;
    double sampleRate = 0.0;
    int blockSize = 0;
    int channels = 0;
    int warmupBlocks = 0;
    std::vector<ParameterOverride> parameterOverrides;

    if (! parseFileOption (options, "--plugin", pluginFile, error)
        || ! parseFileOption (options, "--in", inputFile, error)
        || ! parseFileOption (options, "--outdir", outputDir, error)
        || ! parseDoubleOption (options, "--sr", sampleRate, error)
        || ! parseIntOption (options, "--bs", blockSize, error)
        || ! parseIntOption (options, "--ch", channels, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    if (const auto warmup = options.getValue ("--warmup"); warmup.has_value())
    {
        warmupBlocks = warmup->getIntValue();
        if (warmupBlocks < 0)
        {
            std::cerr << "Invalid --warmup value: " << *warmup << std::endl;
            return 1;
        }
    }

    if (const auto overridesText = options.getValue ("--set-params"); overridesText.has_value())
    {
        if (! parseParameterOverrides (*overridesText, parameterOverrides, error))
        {
            std::cerr << error << std::endl;
            return 1;
        }
    }

    if (blockSize <= 0 || channels <= 0)
    {
        std::cerr << "Block size and channels must be positive." << std::endl;
        return 1;
    }

    LoadedWave dryWave;
    if (! loadWaveFile (inputFile, dryWave, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    juce::AudioBuffer<float> dryBuffer (channels, dryWave.buffer.getNumSamples());
    copyWithChannelMatch (dryWave.buffer, dryBuffer);

    const auto description = findPluginDescription (pluginFile, error);
    if (! description.has_value())
    {
        std::cerr << error << std::endl;
        return 1;
    }

    auto plugin = createPluginInstance (*description, sampleRate, blockSize, error);
    if (plugin == nullptr)
    {
        std::cerr << "Failed to instantiate plugin: " << error << std::endl;
        return 1;
    }

    configurePlugin (*plugin, channels, sampleRate, blockSize);

    if (! applyParameterOverrides (*plugin, parameterOverrides, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    juce::AudioBuffer<float> wetBuffer (channels, dryBuffer.getNumSamples());
    juce::AudioBuffer<float> ioBuffer (channels, blockSize);
    juce::MidiBuffer midiBuffer;

    for (int i = 0; i < warmupBlocks; ++i)
    {
        ioBuffer.clear();
        plugin->processBlock (ioBuffer, midiBuffer);
        midiBuffer.clear();
    }

    for (int pos = 0; pos < dryBuffer.getNumSamples(); pos += blockSize)
    {
        const auto numThisBlock = juce::jmin (blockSize, dryBuffer.getNumSamples() - pos);

        for (int ch = 0; ch < channels; ++ch)
            ioBuffer.copyFrom (ch, 0, dryBuffer, ch, pos, numThisBlock);

        if (numThisBlock < blockSize)
            ioBuffer.clear (numThisBlock, blockSize - numThisBlock);

        juce::AudioBuffer<float> ioView (ioBuffer.getArrayOfWritePointers(), channels, numThisBlock);
        midiBuffer.clear();
        plugin->processBlock (ioView, midiBuffer);

        for (int ch = 0; ch < channels; ++ch)
            wetBuffer.copyFrom (ch, pos, ioBuffer, ch, 0, numThisBlock);
    }

    plugin->releaseResources();

    const auto wetFile = outputDir.getChildFile ("wet.wav");
    if (! writeWaveFile (wetFile, wetBuffer, sampleRate, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    std::cout << "Wrote: " << wetFile.getFullPathName() << std::endl;
    return 0;
}

int runAnalyze (const ParsedOptions& options)
{
    juce::String error;
    juce::File dryFile;
    juce::File wetFile;
    juce::File outputDir;

    if (! parseFileOption (options, "--dry", dryFile, error)
        || ! parseFileOption (options, "--wet", wetFile, error)
        || ! parseFileOption (options, "--outdir", outputDir, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    const auto autoAlign = options.hasFlag ("--auto-align");
    const auto nullRequested = options.hasFlag ("--null");

    LoadedWave dryWave;
    LoadedWave wetWave;
    if (! loadWaveFile (dryFile, dryWave, error) || ! loadWaveFile (wetFile, wetWave, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    if (dryWave.sampleRate <= 0.0 || wetWave.sampleRate <= 0.0)
    {
        std::cerr << "Invalid sample rate in input files." << std::endl;
        return 1;
    }

    const auto channels = juce::jmin (dryWave.buffer.getNumChannels(), wetWave.buffer.getNumChannels());
    if (channels <= 0)
    {
        std::cerr << "Dry/Wet files must have at least one channel." << std::endl;
        return 1;
    }

    auto lagSamples = 0;
    if (autoAlign)
    {
        const auto dryMono = makeMonoSignal (dryWave.buffer, channels);
        const auto wetMono = makeMonoSignal (wetWave.buffer, channels);
        lagSamples = findBestLag (dryMono, wetMono, 4096);
    }

    const auto dryStart = juce::jmax (0, -lagSamples);
    const auto wetStart = juce::jmax (0, lagSamples);
    const auto overlap = juce::jmin (dryWave.buffer.getNumSamples() - dryStart, wetWave.buffer.getNumSamples() - wetStart);

    if (overlap <= 0)
    {
        std::cerr << "No overlap between dry and wet signals after alignment." << std::endl;
        return 1;
    }

    juce::AudioBuffer<float> deltaBuffer (channels, overlap);
    bool hasInvalidValues = hasNaNOrInf (dryWave.buffer) || hasNaNOrInf (wetWave.buffer);

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* dryData = dryWave.buffer.getReadPointer (ch, dryStart);
        const auto* wetData = wetWave.buffer.getReadPointer (ch, wetStart);
        auto* deltaData = deltaBuffer.getWritePointer (ch);

        for (int i = 0; i < overlap; ++i)
        {
            deltaData[i] = wetData[i] - dryData[i];

            if (! std::isfinite (deltaData[i]))
                hasInvalidValues = true;
        }
    }

    if (! outputDir.createDirectory())
    {
        std::cerr << "Failed to create output directory: " << outputDir.getFullPathName() << std::endl;
        return 1;
    }

    const auto deltaFile = outputDir.getChildFile ("delta.wav");
    if (! writeWaveFile (deltaFile, deltaBuffer, dryWave.sampleRate, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    const auto rmsDryDb = computeRmsDb (dryWave.buffer, dryStart, overlap, channels);
    const auto rmsWetDb = computeRmsDb (wetWave.buffer, wetStart, overlap, channels);
    const auto rmsDeltaDb = computeRmsDb (deltaBuffer, 0, deltaBuffer.getNumSamples(), channels);
    const auto peakDeltaDb = computePeakDb (deltaBuffer, 0, deltaBuffer.getNumSamples(), channels);

    const auto metricsFile = outputDir.getChildFile ("metrics.json");
    if (! writeMetricsJson (metricsFile,
                            dryWave.sampleRate,
                            channels,
                            dryWave.buffer.getNumSamples(),
                            wetWave.buffer.getNumSamples(),
                            overlap,
                            lagSamples,
                            autoAlign,
                            nullRequested,
                            rmsDryDb,
                            rmsWetDb,
                            rmsDeltaDb,
                            peakDeltaDb,
                            hasInvalidValues,
                            error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    std::cout << "Wrote: " << deltaFile.getFullPathName() << "\n"
              << "Wrote: " << metricsFile.getFullPathName() << std::endl;

    if (hasInvalidValues)
    {
        std::cerr << "Analyze failed: detected NaN/Inf in dry/wet/delta signals." << std::endl;
        return 2;
    }

    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.isEmpty() || args[0] == "--help" || args[0] == "-h")
    {
        printUsage();
        return 0;
    }

    const auto command = args[0];
    const auto options = parseOptions (args, 1);

    if (options.error.isNotEmpty())
    {
        std::cerr << options.error << std::endl;
        printUsage();
        return 1;
    }

    if (command == "dump-params")
        return runDumpParams (options);

    if (command == "render")
        return runRender (options);

    if (command == "analyze")
        return runAnalyze (options);

    std::cerr << "Unknown command: " << command << std::endl;
    printUsage();
    return 1;
}
