#ifndef CVBASIC_ENGINE_H
#define CVBASIC_ENGINE_H

#include "CvBasicEngine_global.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QByteArray>
#include <memory>

/**
 * @file CvBasicEngine.h
 * @brief Public Qt/C++ facade for the cvBasic interpreter DLL.
 *
 * This header is intentionally small and stable.  It exposes only Qt types and
 * the exported CvBasicEngine class; it does not include the internal script
 * runtime, parser, command registry, image object classes or IDE widgets.  Code
 * using the DLL should include this file (or CvBasicEngineApi.h) and should not
 * depend on files under the script/ directory.
 */

/**
 * @class CvBasicEngine
 * @brief Qt QObject wrapper around the cvBasic interpreter.
 *
 * CvBasicEngine executes cvBasic source text inside a headless host.  The host
 * never opens IDE windows.  Text, progress, images and future visual objects are
 * reported through Qt signals, so an external application such as the current
 * IDE or cvBasic II can decide how to present them.
 *
 * The public API deliberately uses opaque exported-object handles instead of
 * returning internal classes such as ImageObject or cv::Mat.  This keeps the ABI
 * boundary narrow and prevents client projects from depending on interpreter
 * implementation headers.
 */
class CVBASICENGINE_API CvBasicEngine : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Creates an interpreter engine.
     * @param parent Optional Qt parent object.
     */
    explicit CvBasicEngine(QObject* parent = nullptr);

    /**
     * @brief Destroys the engine and releases runtime state and exported objects.
     */
    ~CvBasicEngine() override;

    /**
     * @brief Executes cvBasic source text.
     *
     * If auto-call-main is enabled and the source contains SUB MAIN(), the engine
     * automatically appends the required CALL MAIN() wrapper used by the IDE.
     *
     * @param scriptText UTF-16 Qt string containing the script source.
     * @param virtualScriptPath Optional virtual path used for diagnostics and
     *        relative file resolution.
     * @return true on successful execution, false on syntax/runtime error.
     * @see lastErrorText(), lastErrorLine(), executionStarted(), executionFinished()
     */
    Q_INVOKABLE bool executeString(const QString& scriptText,
                                   const QString& virtualScriptPath = QString());

    /**
     * @brief Checks script syntax without executing the script.
     * @param scriptText UTF-16 Qt string containing the script source.
     * @param virtualScriptPath Optional virtual path used for diagnostics.
     * @return true if the script is syntactically valid, false otherwise.
     */
    Q_INVOKABLE bool checkSyntaxString(const QString& scriptText,
                                       const QString& virtualScriptPath = QString());

    /**
     * @brief Defines command-line style context visible to the script.
     * @param scriptPath Script path exposed to the runtime.
     * @param arguments Positional arguments exposed to the runtime.
     * @param namedParameters Named parameters also injected as script variables.
     */
    Q_INVOKABLE void setCommandLineContext(const QString& scriptPath,
                                           const QStringList& arguments = QStringList(),
                                           const QVariantMap& namedParameters = QVariantMap());

    /**
     * @brief Defines optional OpenVision job/work-directory context.
     * @param jobPath OpenVision job path.
     * @param workDir OpenVision working directory.
     */
    Q_INVOKABLE void setOpenVisionContext(const QString& jobPath, const QString& workDir);

    /** @brief Enables or disables automatic CALL MAIN() insertion. */
    Q_INVOKABLE void setAutoCallMainEnabled(bool enabled);

    /** @brief Returns whether automatic CALL MAIN() insertion is enabled. */
    Q_INVOKABLE bool autoCallMainEnabled() const;

    /**
     * @brief Controls whether runtime variables are cleared before each execution.
     *
     * The default is true.  When true, exported visual objects from the previous
     * run are also released before a new execution begins.
     */
    Q_INVOKABLE void setClearRuntimeBeforeExecute(bool enabled);

    /** @brief Returns the clear-runtime-before-execute flag. */
    Q_INVOKABLE bool clearRuntimeBeforeExecute() const;

    /** @brief Sets or overwrites a script variable with a string value. */
    Q_INVOKABLE void setVariableText(const QString& name, const QString& value);

    /** @brief Sets or overwrites a script variable with a numeric value. */
    Q_INVOKABLE void setVariableNumber(const QString& name, double value);

    /** @brief Sets or overwrites a script variable with a boolean value. */
    Q_INVOKABLE void setVariableBool(const QString& name, bool value);

    /** @brief Clears runtime variables and exported visual-object handles. */
    Q_INVOKABLE void clearRuntime();

    /** @brief Requests cooperative stop at the next interpreter/host check. */
    Q_INVOKABLE void requestStop();

    /** @brief Clears a previous cooperative stop request. */
    Q_INVOKABLE void clearStopRequest();

    /** @brief Returns the last formatted error message, or an empty string. */
    Q_INVOKABLE QString lastErrorText() const;

    /** @brief Returns the last error line, or -1 when no line is available. */
    Q_INVOKABLE int lastErrorLine() const;

    /** @brief Returns the complete textual log captured by the headless host. */
    Q_INVOKABLE QString lastLogText() const;

    /**
     * @brief Registers or replaces a host-owned cvBasic command.
     *
     * The command is installed in the interpreter's live ScriptCommandRegistry,
     * exactly like a native statement. Arguments are ordinary cvBasic
     * expressions and are evaluated before the callback is invoked.
     *
     * The receiver method must be a Qt-invokable method/slot with this signature:
     *
     *   QVariantMap method(const QString& commandName,
     *                      const QVariantList& arguments,
     *                      int lineNumber)
     *
     * Return { "ok": true } on success. On failure return
     * { "ok": false, "error": "message" }; the message becomes a normal
     * cvBasic runtime error at the calling script line.
     *
     * Host callbacks are synchronous. If the receiver belongs to another Qt
     * thread, the engine uses a blocking queued invocation so the script resumes
     * only after the host command has completed.
     *
     * @param publicName Public cvBasic spelling, e.g. "DROSO.RUN" or "RUNMODEL".
     * @param minArgs Minimum evaluated argument count.
     * @param maxArgs Maximum argument count; -1 means variadic.
     * @param signature Help signature exposed by languageSymbolInfo().
     * @param description Help text exposed by languageSymbolInfo().
     * @param receiver QObject that owns the callback method.
     * @param method Callback method name (without parentheses/signature).
     * @return true if registered; false on invalid/conflicting metadata.
     */
    bool registerHostCommand(const QString& publicName,
                             int minArgs,
                             int maxArgs,
                             const QString& signature,
                             const QString& description,
                             QObject* receiver,
                             const QByteArray& method);

    /** @brief Removes one command previously installed with registerHostCommand(). */
    bool unregisterHostCommand(const QString& publicName);

    /** @brief Removes all commands registered by the current host. */
    void clearHostCommands();

    /** @brief Returns only the command names registered by the current host. */
    QStringList hostCommandNames() const;

    /** @brief Returns the last registration error produced by the host-command API. */
    QString lastHostCommandError() const;

    /** @brief Returns the registered cvBasic command names. */
    Q_INVOKABLE QStringList commandNames() const;

    /** @brief Returns the registered cvBasic function names. */
    Q_INVOKABLE QStringList functionNames() const;

    /** @brief Returns language keywords known by the interpreter. */
    Q_INVOKABLE QStringList languageKeywords() const;

    /**
     * @brief Returns public help metadata for a command, function or keyword.
     *
     * The map uses only Qt value types and never exposes ScriptInterpreter or
     * ScriptLanguageCatalog classes.  Typical keys are: name, kind, signature,
     * description and valid.  This is the preferred bridge for cvBasic II help
     * panes and quick-launch contextual documentation.
     *
     * @param symbol Command/function/keyword name to inspect.
     * @return Public metadata map; valid=false when the symbol is unknown.
     */
    Q_INVOKABLE QVariantMap languageSymbolInfo(const QString& symbol) const;

    /** @brief Returns a diagnostic report for loaded user libraries. */
    Q_INVOKABLE QString userLibraryReport() const;

    /**
     * @brief Returns a public, host-safe snapshot of the current runtime state.
     *
     * The returned QVariantMap is intended for IDE/inspector panels.  It uses
     * only Qt value types and never exposes ScriptRuntime, ScriptValue, cv::Mat,
     * ImageObject or other private implementation classes across the DLL
     * boundary.
     *
     * Typical top-level keys are: variables, apVariables, cxVariables,
     * openFiles, exportedObjects, scriptPath, scriptDir, arguments,
     * namedParameters, variableScopeDepth, apPrecision, lastErrorText and
     * lastErrorLine.  The exact set may grow over time while staying
     * QVariantMap-based.
     *
     * @return Descriptive runtime snapshot suitable for a host-side tree/table.
     */
    Q_INVOKABLE QVariantMap runtimeSnapshotInfo() const;

    /**
     * @brief Returns metadata for an exported object handle.
     *
     * Handles are emitted by imageObjectProduced() and hostEventProduced().  They
     * are opaque values owned by the engine.  A client should not cast them to
     * internal pointers.
     *
     * @param handle Opaque handle previously emitted by the engine.
     * @return A QVariantMap with descriptive fields, or an empty map if invalid.
     */
    Q_INVOKABLE QVariantMap exportedObjectInfo(qulonglong handle) const;

    /**
     * @brief Converts an exported image/stack frame to QImage.
     *
     * This is the safe public bridge for cvBasic II viewers.  Internally the
     * engine may store cv::Mat/ImageObject instances; clients receive only a
     * QImage copy suitable for display.
     *
     * @param handle Opaque image handle from imageObjectProduced().
     * @param frameIndex Frame index for stacks, zero-based.  Ignored for single
     *        images.
     * @return A detached QImage copy, or a null image if the handle/frame is invalid.
     */
    Q_INVOKABLE QImage exportedImageAsQImage(qulonglong handle, int frameIndex = 0) const;


    /**
     * @brief Registers or replaces a host-owned ROI selection for a script-visible window title.
     *
     * This is an intermediate host-service bridge for cvBasic II.  Scripts that
     * query ROI information by window title, for example ROI.X("Preview") or
     * ROI.MEAN("Preview"), can run without the DLL opening IDE windows.  The
     * host remains responsible for choosing the rectangle, either automatically
     * or through a future mouse-based ROI widget.
     *
     * The API deliberately exposes only primitive Qt/C++ types.  No ScriptHost,
     * ImageObject, cv::Mat or IDE widget type crosses the public DLL boundary.
     *
     * @param windowTitle Title used by the script.  Empty or "ACTIVE" means the
     *        current active exported image, when available.
     * @param x Left coordinate in image pixels.
     * @param y Top coordinate in image pixels.
     * @param width ROI width in pixels.
     * @param height ROI height in pixels.
     * @param frameIndex Stack frame index, zero-based.
     * @param changeKind Optional textual origin such as "host_auto",
     *        "host_user" or "host_test".
     * @return true if the ROI was accepted and stored, false if it was invalid.
     * @see clearHostRoiSelection(), hostRoiSelectionInfo()
     */
    Q_INVOKABLE bool setHostRoiSelection(const QString& windowTitle,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         int frameIndex = 0,
                                         const QString& changeKind = QString());

    /**
     * @brief Clears a host-owned ROI selection.
     * @param windowTitle Title used by the script.  Empty or "ACTIVE" clears
     *        the active-image ROI if one exists.
     * @return true when the request was handled.
     */
    Q_INVOKABLE bool clearHostRoiSelection(const QString& windowTitle = QString());

    /**
     * @brief Returns metadata for a host-owned ROI selection.
     *
     * The returned map contains fields such as valid, title, x, y, width,
     * height, frameIndex, revision and changeKind.  When no ROI is available,
     * valid is false and error contains a human-readable explanation.
     *
     * @param windowTitle Title used by the script.  Empty or "ACTIVE" queries
     *        the active-image ROI if one exists.
     */
    Q_INVOKABLE QVariantMap hostRoiSelectionInfo(const QString& windowTitle = QString()) const;

    /**
     * @brief Enables or disables the transitional legacy-host compatibility glue.
     *
     * The glue is intentionally small: it does not reintroduce the old IDE into
     * the DLL.  It only lets legacy scripts continue when they call services
     * formerly provided by MainWindow/ImageView and cvBasic II has not yet
     * supplied a concrete interactive result.  For ROI calls this means:
     *
     * - symbolic window titles may resolve to the active exported image;
     * - WAIT_ROI can synthesize a safe central ROI if no user ROI exists yet.
     *
     * A final cvBasic II host can keep this enabled as a fallback, or disable it
     * once every interactive service is implemented explicitly.
     *
     * @param enabled true to enable compatibility fallback behavior.
     * @see hostCompatibilityGlueEnabled(), setHostRoiSelection()
     */
    Q_INVOKABLE void setHostCompatibilityGlueEnabled(bool enabled);

    /** @brief Returns whether transitional legacy-host compatibility glue is enabled. */
    Q_INVOKABLE bool hostCompatibilityGlueEnabled() const;

    /** @brief Releases a single exported object handle. */
    Q_INVOKABLE void releaseExportedObject(qulonglong handle);

    /** @brief Releases all exported object handles owned by this engine. */
    Q_INVOKABLE void clearExportedObjects();

signals:
    /** @brief Emitted for each PRINT/log line produced by the script. */
    void linePrinted(const QString& text);

    /** @brief Emitted when the script requests an informational message. */
    void infoMessage(const QString& title, const QString& text);

    /** @brief Emitted when a long operation starts. */
    void progressStarted(const QString& text, int maximum);

    /** @brief Emitted when a long operation reports progress. */
    void progressChanged(int value, int maximum, const QString& text);

    /** @brief Emitted when a long operation finishes. */
    void progressFinished();

    /**
     * @brief Emitted when the script produces an image/stack for display.
     *
     * @param title Display title requested by the script.
     * @param handle Opaque engine-owned handle.  Pass it to exportedImageAsQImage()
     *        or exportedObjectInfo(); never cast it to an internal type.
     * @param imageInfo Descriptive metadata: width, height, channels,
     *        frameCount, fps, kind, handle, title and objectClass.
     */
    void imageObjectProduced(const QString& title, qulonglong handle, const QVariantMap& imageInfo);

    /**
     * @brief Generic visual/host event for future IDE and cvBasic II integration.
     *
     * eventClass is intentionally textual (for example "image", "mat3d",
     * "dataplot", "gl", "vectorcanvas") so new host-side features can be added
     * without changing this public header.
     */
    void hostEventProduced(const QString& eventClass,
                           const QString& title,
                           const QVariantMap& payload);

    /** @brief Emitted just before executeString() begins execution. */
    void executionStarted();

    /** @brief Emitted after executeString() completes. */
    void executionFinished(bool ok, const QString& errorText, int errorLine);

private:
    void applyContext(const QString& virtualScriptPath);
    void setLastError(const QString& text, int line);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

#endif // CVBASIC_ENGINE_H
