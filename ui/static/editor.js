
let editorInstance = null;
let monacoInstance = null;
let changeSubscription = null;

/**
 * Initialize Monaco Editor using monaco-esm.
 * Works in a browser without a bundler.
 */
export async function initMonaco(initialLanguage = "c", initialValue = "") {
    if (editorInstance) {
        return editorInstance;
    }

    // Import the documented monaco-esm exports.
    const { monaco, loadCss } = await import(
        "https://esm.sh/monaco-esm"
    );

    loadCss();

    const container = document.getElementById("monaco-container");

    if (!container) {
        throw new Error(
            "Monaco container #monaco-container was not found."
        );
    }

    monacoInstance = monaco;

    editorInstance = monaco.editor.create(container, {
        value: initialValue || "",
        language: initialLanguage || "c",
        theme: "vs-dark",

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
        fontLigatures: true,

        // Editing
        tabSize: 4,
        insertSpaces: true,
        autoIndent: "full",
        formatOnPaste: true,
        formatOnType: true,

        // Suggestions
        quickSuggestions: true,
        suggestOnTriggerCharacters: true,
        acceptSuggestionOnEnter: "on",

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
