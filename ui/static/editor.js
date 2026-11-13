
let editorInstance = null;
let monacoInstance = null;
let changeSubscription = null;
let monacoLoadPromise = null;

let themesDefined = false;

function defineEditorThemes(monaco) {
    if (themesDefined) return;

    monaco.editor.defineTheme("codearena-light", {
        base: "vs",
        inherit: true,
        rules: [
            { token: "", foreground: "2B1014", background: "FFFFFF" },
            { token: "comment", foreground: "936668", fontStyle: "italic" },
            { token: "keyword", foreground: "E84B55" },
            { token: "string", foreground: "2F7D4F" },
            { token: "number", foreground: "9A6A1C" },
            { token: "type", foreground: "A86B34" },
            { token: "function", foreground: "B3352E" }
        ],
        colors: {
            "editor.background": "#FFFFFF",
            "editor.foreground": "#2B1014",
            "editorLineNumber.foreground": "#936668",
            "editorLineNumber.activeForeground": "#2B1014",
            "editorCursor.foreground": "#E84B55",
            "editor.selectionBackground": "#E84B5533",
            "editor.inactiveSelectionBackground": "#E84B551F",
            "editor.lineHighlightBackground": "#FBEDE8",
            "editor.lineHighlightBorder": "#00000000",
            "editorIndentGuide.background1": "#F3DED6",
            "editorIndentGuide.activeBackground1": "#93666866",
            "editorGutter.background": "#FFFFFF",
            "editorWidget.background": "#FFFFFF",
            "editorWidget.border": "#F3DED6",
            "scrollbarSlider.background": "#93666833",
            "scrollbarSlider.hoverBackground": "#93666855",
            "scrollbarSlider.activeBackground": "#93666877",
            "minimap.background": "#FFFFFF"
        }
    });

    monaco.editor.defineTheme("codearena-dark", {
        base: "vs-dark",
        inherit: true,
        rules: [
            { token: "", foreground: "FBEDE8", background: "23181C" },
            { token: "comment", foreground: "B08F8C", fontStyle: "italic" },
            { token: "keyword", foreground: "F26A73" },
            { token: "string", foreground: "5FD68E" },
            { token: "number", foreground: "F0C060" },
            { token: "type", foreground: "C08A4D" },
            { token: "function", foreground: "FF7B72" }
        ],
        colors: {
            "editor.background": "#23181C",
            "editor.foreground": "#FBEDE8",
            "editorLineNumber.foreground": "#B08F8C",
            "editorLineNumber.activeForeground": "#FBEDE8",
            "editorCursor.foreground": "#E84B55",
            "editor.selectionBackground": "#E84B5544",
            "editor.inactiveSelectionBackground": "#E84B5526",
            "editor.lineHighlightBackground": "#2D2025",
            "editor.lineHighlightBorder": "#00000000",
            "editorIndentGuide.background1": "#2D2025",
            "editorIndentGuide.activeBackground1": "#B08F8C66",
            "editorGutter.background": "#23181C",
            "editorWidget.background": "#23181C",
            "editorWidget.border": "#2D2025",
            "scrollbarSlider.background": "#B08F8C33",
            "scrollbarSlider.hoverBackground": "#B08F8C55",
            "scrollbarSlider.activeBackground": "#B08F8C77",
            "minimap.background": "#23181C"
        }
    });

    themesDefined = true;
}

function loadMonaco() {
    if (monacoInstance) return Promise.resolve(monacoInstance);
    if (monacoLoadPromise) return monacoLoadPromise;

    monacoLoadPromise = new Promise((resolve, reject) => {
        if (window.monaco && window.monaco.editor) {
            monacoInstance = window.monaco;
            defineEditorThemes(monacoInstance);
            resolve(monacoInstance);
            return;
        }

        const base = "https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.52.2/min";

        function onAmdReady() {
            try {
                window.require.config({ paths: { vs: `${base}/vs` } });
                window.MonacoEnvironment = {
                    getWorkerUrl: function () {
                        const proxy = `
                            self.MonacoEnvironment = { baseUrl: '${base}/' };
                            importScripts('${base}/vs/base/worker/workerMain.min.js');
                        `;
                        return `data:text/javascript;charset=utf-8,${encodeURIComponent(proxy)}`;
                    }
                };
                window.require(["vs/editor/editor.main"], () => {
                    monacoInstance = window.monaco;
                    defineEditorThemes(monacoInstance);
                    resolve(monacoInstance);
                }, reject);
            } catch (e) {
                reject(e);
            }
        }

        if (window.require && window.require.config) {
            onAmdReady();
            return;
        }

        const script = document.createElement("script");
        script.src = `${base}/vs/loader.min.js`;
        script.async = true;
        script.onload = onAmdReady;
        script.onerror = () => reject(new Error("Failed to load Monaco loader"));
        document.head.appendChild(script);
    });

    return monacoLoadPromise;
}

/**
 * Initialize Monaco Editor using Monaco's AMD loader (CDN).
 * Works in a browser without a bundler.
 */
export async function initMonaco(initialLanguage = "c", initialValue = "") {
    if (editorInstance) return editorInstance;

    const monaco = await loadMonaco();
    const container = document.getElementById("monaco-container");
    if (!container) {
        throw new Error("Monaco container #monaco-container was not found.");
    }

    const initialTheme = (document.documentElement.dataset.theme === "dark")
        ? "codearena-dark"
        : "codearena-light";

    editorInstance = monaco.editor.create(container, {
        value: initialValue || "",
        language: initialLanguage || "c",
        theme: initialTheme,

        // Layout
        automaticLayout: true,
        scrollBeyondLastLine: false,
        wordWrap: "on",
        padding: {
            top: 12,
            bottom: 12
        },

        // Typography
        fontSize: 14,
        lineHeight: 22,
        fontFamily: "Consolas, 'Courier New', monospace",
        fontLigatures: false,

        // Editing
        tabSize: 4,
        insertSpaces: true,
        autoIndent: "full",
        formatOnPaste: false,
        formatOnType: false,

        // Suggestions
        quickSuggestions: false,
        suggestOnTriggerCharacters: false,
        acceptSuggestionOnEnter: "off",

        // Visual features
        minimap: {
            enabled: false
        },
        bracketPairColorization: {
            enabled: true
        },
        matchBrackets: "always",
        renderLineHighlight: "all",
        cursorBlinking: "smooth",
        smoothScrolling: true,

        // Accessibility
        ariaLabel: "CodeArena source code editor"
    });

    return editorInstance;
}

/**
 * Return the current editor instance.
 */
export function getEditor() {
    return editorInstance;
}

/**
 * Get the code currently written in the editor.
 */
export function getEditorValue() {
    if (!editorInstance) {
        return "";
    }

    return editorInstance.getValue();
}

/**
 * Replace the editor contents.
 */
export function setEditorValue(value) {
    if (!editorInstance) {
        return;
    }

    editorInstance.setValue(value ?? "");
}

/**
 * Change the editor language.
 * Examples: c, cpp, python, java, javascript.
 */
export function setEditorLanguage(language) {
    if (!editorInstance || !monacoInstance) {
        return;
    }

    const model = editorInstance.getModel();

    if (model) {
        monacoInstance.editor.setModelLanguage(
            model,
            language || "c"
        );
    }
}

/**
 * Listen for code changes.
 * Calling this again replaces the previous listener.
 */
export function onEditorChange(callback) {
    if (!editorInstance || typeof callback !== "function") {
        return;
    }

    if (changeSubscription) {
        changeSubscription.dispose();
    }

    changeSubscription = editorInstance.onDidChangeModelContent(() => {
        callback(getEditorValue());
    });
}

/**
 * Set the editor's read-only state.
 */
export function setEditorReadOnly(readOnly = true) {
    if (!editorInstance) {
        return;
    }

    editorInstance.updateOptions({
        readOnly: Boolean(readOnly)
    });
}

/**
 * Focus the editor.
 */
export function focusEditor() {
    if (editorInstance) {
        editorInstance.focus();
    }
}

/**
 * Clear the editor contents.
 */
export function clearEditor() {
    setEditorValue("");
}

/**
 * Dispose the editor and its listeners.
 */
export function disposeEditor() {
    if (changeSubscription) {
        changeSubscription.dispose();
        changeSubscription = null;
    }

    if (editorInstance) {
        editorInstance.dispose();
        editorInstance = null;
    }

    monacoInstance = null;
}

/**
 * Update specific Monaco editor options at runtime.
 * @param {object} options - e.g. { minimap: { enabled: true }, tabSize: 2 }
 */
export function updateEditorOptions(options) {
    if (editorInstance) {
        editorInstance.updateOptions(options);
    }
}

/**
 * Change the Monaco editor theme to match site theme.
 */
export function setEditorTheme(theme) {
    if (!monacoInstance) return;
    defineEditorThemes(monacoInstance);
    const name = (theme === "dark") ? "codearena-dark" : "codearena-light";
    monacoInstance.editor.setTheme(name);
}