'use client'; // Add this directive for using hooks

import React, { useState, useEffect, ChangeEvent } from 'react';
import Image from 'next/image';

// --- Interfaces ---
type Message = {
  id: string | number;
  sender: 'user' | 'bot';
  text: string;
};

type LogEntry = {
  id: string | number;
  timestamp: string;
  level: string;
  message: string;
};

// --- API Base URL ---
const API_URL = process.env.NEXT_PUBLIC_API_URL || 'http://localhost:8000'; // Fallback for local dev outside Docker

// --- Components ---

const ChatInterface = () => {
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState('');
  const [isLoading, setIsLoading] = useState(false);

  const handleSend = async () => {
    if (!input.trim()) return;

    const userMessage: Message = { id: Date.now(), sender: 'user', text: input };
    setMessages(prev => [...prev, userMessage]);
    setInput('');
    setIsLoading(true);

    try {
      // Replace with your actual chat endpoint and request/response structure
      const response = await fetch(`${API_URL}/api/chat`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ query: input }),
      });

      if (!response.ok) {
        throw new Error(`API Error: ${response.statusText}`);
      }

      const data = await response.json();
      const botMessage: Message = { id: Date.now() + 1, sender: 'bot', text: data.response || 'Sorry, I could not process that.' }; // Adjust based on actual API response
      setMessages(prev => [...prev, botMessage]);

    } catch (error) {
      console.error("Error sending message:", error);
      const errorMessage: Message = { id: Date.now() + 1, sender: 'bot', text: 'Error connecting to backend.' };
      setMessages(prev => [...prev, errorMessage]);
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="bg-gray-100 p-4 rounded-lg shadow-md h-96 flex flex-col">
      <h2 className="text-xl font-semibold mb-4 text-gray-800">Chat Interface</h2>
      <div className="flex-grow border rounded p-2 mb-2 overflow-y-auto bg-white space-y-2">
        {messages.map((msg) => (
          <div key={msg.id} className={`p-2 rounded ${msg.sender === 'user' ? 'bg-blue-100 text-right ml-auto' : 'bg-gray-200 text-left mr-auto'} max-w-[80%]`}>
            {msg.text}
          </div>
        ))}
        {isLoading && <div className="p-2 text-gray-500 text-left">Bot is thinking...</div>}
      </div>
      <div className="flex">
        <input
          type="text"
          placeholder="Type your message..."
          className="flex-grow border rounded p-2 mr-2 disabled:bg-gray-200"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyPress={(e) => e.key === 'Enter' && !isLoading && handleSend()}
          disabled={isLoading}
        />
        <button
          className="bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded disabled:opacity-50"
          onClick={handleSend}
          disabled={isLoading}
        >
          Send
        </button>
      </div>
    </div>
  );
};

const LogTableViewer = () => {
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchLogs = async () => {
      setIsLoading(true);
      setError(null);
      try {
        // Replace with your actual logs endpoint
        const response = await fetch(`${API_URL}/api/logs`);
        if (!response.ok) {
          throw new Error(`API Error: ${response.statusText}`);
        }
        const data: LogEntry[] = await response.json(); // Adjust based on actual API response
        // Add unique IDs if not present (example)
        setLogs(data.map((log, index) => ({ ...log, id: log.id || index })));
      } catch (err) {
        console.error("Error fetching logs:", err);
        setError(err instanceof Error ? err.message : 'Failed to fetch logs.');
        setLogs([]); // Clear logs on error
      } finally {
        setIsLoading(false);
      }
    };

    fetchLogs();
    // Optionally, set up polling
    const intervalId = setInterval(fetchLogs, 10000); // Refresh every 10 seconds
    return () => clearInterval(intervalId); // Cleanup interval on unmount

  }, []);

  return (
    <div className="bg-gray-100 p-4 rounded-lg shadow-md h-[calc(24rem+6rem)] flex flex-col"> {/* Adjusted height */} 
      <h2 className="text-xl font-semibold mb-4 text-gray-800">Log Viewer</h2>
      <div className="flex-grow overflow-y-auto">
        {isLoading && <p className="text-center text-gray-500">Loading logs...</p>}
        {error && <p className="text-center text-red-500">Error: {error}</p>}
        {!isLoading && !error && logs.length === 0 && <p className="text-center text-gray-500">No logs found.</p>}
        {!isLoading && !error && logs.length > 0 && (
          <table className="w-full table-auto border-collapse">
            <thead className="sticky top-0 bg-gray-200 z-10">
              <tr>
                <th className="border px-4 py-2">Timestamp</th>
                <th className="border px-4 py-2">Level</th>
                <th className="border px-4 py-2">Message</th>
              </tr>
            </thead>
            <tbody>
              {logs.map((log) => (
                <tr key={log.id} className="hover:bg-gray-50">
                  <td className="border px-4 py-2 text-sm">{log.timestamp}</td>
                  <td className="border px-4 py-2 text-sm">{log.level}</td>
                  <td className="border px-4 py-2 text-sm">{log.message}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
};

const LogAnalyzer = () => {
  const [analysisTask, setAnalysisTask] = useState<string>('');
  const [isAnalyzing, setIsAnalyzing] = useState(false);
  const [analysis, setAnalysis] = useState<{
    success: boolean;
    analysis: string;
    has_visualizations: boolean;
    visualizations: string[];
    error: string | null;
  } | null>(null);
  const [selectedTemplate, setSelectedTemplate] = useState<string>('');

  // Predefined analysis templates
  const analysisTemplates = [
    { id: 'level-count', name: 'Count by Log Level', query: 'Count logs by level and show distribution' },
    { id: 'time-dist', name: 'Time Distribution', query: 'Show time distribution of logs' },
    { id: 'template-trend', name: 'Template Patterns', query: 'Analyze template trends in logs' },
    { id: 'custom', name: 'Custom Analysis', query: '' }
  ];

  const handleTemplateChange = (e: ChangeEvent<HTMLSelectElement>) => {
    const templateId = e.target.value;
    setSelectedTemplate(templateId);
    
    if (templateId === 'custom') {
      setAnalysisTask('');
    } else {
      const template = analysisTemplates.find(t => t.id === templateId);
      if (template) {
        setAnalysisTask(template.query);
      }
    }
  };

  const runAnalysis = async () => {
    if (!analysisTask.trim()) return;
    
    setIsAnalyzing(true);
    setAnalysis(null);
    
    try {
      const response = await fetch(`${API_URL}/api/analyze`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ task: analysisTask }),
      });

      if (!response.ok) {
        throw new Error(`API Error: ${response.statusText}`);
      }

      const result = await response.json();
      setAnalysis(result);
    } catch (error) {
      console.error("Error during analysis:", error);
      setAnalysis({
        success: false,
        analysis: '',
        has_visualizations: false,
        visualizations: [],
        error: error instanceof Error ? error.message : 'Unknown error occurred'
      });
    } finally {
      setIsAnalyzing(false);
    }
  };

  return (
    <div className="bg-gray-100 p-4 rounded-lg shadow-md flex flex-col">
      <h2 className="text-xl font-semibold mb-4 text-gray-800">Log Analysis</h2>
      
      <div className="mb-4">
        <label className="block text-sm font-medium text-gray-700 mb-1">
          Analysis Type
        </label>
        <select
          className="w-full p-2 border rounded"
          value={selectedTemplate}
          onChange={handleTemplateChange}
        >
          <option value="">Select analysis type</option>
          {analysisTemplates.map((template) => (
            <option key={template.id} value={template.id}>
              {template.name}
            </option>
          ))}
        </select>
      </div>
      
      <div className="mb-4">
        <label className="block text-sm font-medium text-gray-700 mb-1">
          Analysis Description
        </label>
        <textarea
          className="w-full p-2 border rounded"
          rows={3}
          value={analysisTask}
          onChange={(e) => setAnalysisTask(e.target.value)}
          placeholder="Describe what kind of analysis to perform..."
        />
      </div>
      
      <button
        className="bg-indigo-600 hover:bg-indigo-700 text-white font-bold py-2 px-4 rounded disabled:opacity-50 mb-4"
        onClick={runAnalysis}
        disabled={isAnalyzing || !analysisTask.trim()}
      >
        {isAnalyzing ? 'Analyzing...' : 'Analyze Logs'}
      </button>
      
      {analysis && (
        <div className="mt-4 border-t pt-4">
          <h3 className="text-lg font-medium mb-2">Analysis Result</h3>
          {analysis.error ? (
            <div className="text-red-500">{analysis.error}</div>
          ) : (
            <>
              <div className="mb-4 p-3 bg-white rounded">{analysis.analysis}</div>
              
              {analysis.has_visualizations && analysis.visualizations.length > 0 && (
                <div className="mt-4">
                  <h4 className="text-md font-medium mb-2">Visualizations</h4>
                  <div className="grid grid-cols-1 gap-4">
                    {analysis.visualizations.map((viz, index) => (
                      <div key={index} className="border p-2 bg-white">
                        <Image 
                          src={`data:image/png;base64,${viz}`} 
                          alt={`Visualization ${index+1}`}
                          width={800}
                          height={500}
                          className="mx-auto"
                        />
                      </div>
                    ))}
                  </div>
                </div>
              )}
            </>
          )}
        </div>
      )}
    </div>
  );
};

const LogFileUploader = () => {
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [isUploading, setIsUploading] = useState(false);
  const [uploadStatus, setUploadStatus] = useState<string | null>(null);

  const handleFileChange = (event: ChangeEvent<HTMLInputElement>) => {
    if (event.target.files && event.target.files[0]) {
      setSelectedFile(event.target.files[0]);
      setUploadStatus(null); // Reset status on new file selection
    } else {
      setSelectedFile(null);
    }
  };

  const handleUpload = async () => {
    if (!selectedFile) {
      setUploadStatus('Please select a file first.');
      return;
    }

    setIsUploading(true);
    setUploadStatus('Uploading...');

    const formData = new FormData();
    formData.append('file', selectedFile); // Key 'file' should match backend expectation

    try {
      // Replace with your actual upload endpoint
      const response = await fetch(`${API_URL}/api/upload-log-file`, {
        method: 'POST',
        body: formData, // No 'Content-Type' header needed for FormData, browser sets it
      });

      if (!response.ok) {
         const errorData = await response.text(); // Read error details
         throw new Error(`Upload failed: ${response.statusText} - ${errorData}`);
      }

      const result = await response.json(); // Or response.text() if backend sends text
      setUploadStatus(`Upload successful! ${result.message || ''}`); // Adjust based on backend response
      setSelectedFile(null); // Clear selection after successful upload
      // TODO: Optionally trigger log refresh

    } catch (error) {
      console.error("Error uploading file:", error);
      setUploadStatus(`Upload failed: ${error instanceof Error ? error.message : 'Unknown error'}`);
    } finally {
      setIsUploading(false);
    }
  };

  return (
    <div className="bg-gray-100 p-4 rounded-lg shadow-md">
      <h2 className="text-xl font-semibold mb-4 text-gray-800">Upload Log File</h2>
      <input
        type="file"
        onChange={handleFileChange}
        className="block w-full text-sm text-gray-500
          file:mr-4 file:py-2 file:px-4
          file:rounded-full file:border-0
          file:text-sm file:font-semibold
          file:bg-blue-50 file:text-blue-700
          hover:file:bg-blue-100 mb-2
        "
        accept=".log,.txt" // Specify acceptable file types
        disabled={isUploading}
      />
      {selectedFile && <p className="text-sm text-gray-600 mb-2">Selected: {selectedFile.name}</p>}
      <button
        onClick={handleUpload}
        className="bg-green-500 hover:bg-green-600 text-white font-bold py-2 px-4 rounded disabled:opacity-50"
        disabled={isUploading || !selectedFile}
      >
        {isUploading ? 'Uploading...' : 'Upload'}
      </button>
      {uploadStatus && <p className="mt-2 text-sm text-gray-700">{uploadStatus}</p>}
    </div>
  );
};

// --- Main Page ---

export default function Home() {
  const [activeTab, setActiveTab] = useState<'logs' | 'analysis'>('logs');

  return (
    <main className="flex min-h-screen flex-col items-center justify-between p-8 bg-gray-50">
      <div className="z-10 max-w-6xl w-full items-center justify-between font-mono text-sm lg:flex mb-8">
        <h1 className="text-3xl font-bold text-gray-900">LogAI C++ Test UI</h1>
        
        <div className="flex space-x-4 mt-4 lg:mt-0">
          <button 
            className={`px-4 py-2 font-medium rounded ${activeTab === 'logs' ? 'bg-blue-500 text-white' : 'bg-gray-200'}`}
            onClick={() => setActiveTab('logs')}
          >
            Logs & Chat
          </button>
          <button 
            className={`px-4 py-2 font-medium rounded ${activeTab === 'analysis' ? 'bg-blue-500 text-white' : 'bg-gray-200'}`}
            onClick={() => setActiveTab('analysis')}
          >
            Analysis
          </button>
        </div>
      </div>

      {activeTab === 'logs' && (
        <div className="w-full max-w-6xl grid grid-cols-1 lg:grid-cols-2 gap-8">
          {/* Left Column: Chat and Uploader */}
          <div className="flex flex-col gap-8">
            <ChatInterface />
            <LogFileUploader />
          </div>

          {/* Right Column: Log Viewer */}
          <div className="lg:col-span-1">
            <LogTableViewer />
          </div>
        </div>
      )}
      
      {activeTab === 'analysis' && (
        <div className="w-full max-w-6xl">
          <LogAnalyzer />
        </div>
      )}
    </main>
  );
}
