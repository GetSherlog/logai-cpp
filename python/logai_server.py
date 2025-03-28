#!/usr/bin/env python3
import os
import json
from typing import List, Dict, Any, Optional, Literal
from pydantic import BaseModel, Field

from fastapi import FastAPI, HTTPException, Depends, UploadFile, File, Form, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

# Import our LogAI agent
from logai_agent import LogAIAgent, SearchResult, QueryResult, LogTemplate, TimeRange, CountResult

# Create FastAPI app
app = FastAPI(
    title="LogAI API Server",
    description="API Server for LogAI - AI-powered log analysis",
    version="1.0.0"
)

# Add CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Global agent instance
logai_agent = None

# Request and response models
class InitializeRequest(BaseModel):
    log_file: str
    format: Optional[str] = None

class InitializeResponse(BaseModel):
    success: bool
    message: str

class SearchRequest(BaseModel):
    query: str
    limit: int = 10

class ExecuteQueryRequest(BaseModel):
    query: str

class TemplateRequest(BaseModel):
    template_id: int

class CountOccurrencesRequest(BaseModel):
    pattern: str
    group_by: Optional[str] = None

class FilterByTimeRequest(BaseModel):
    since: Optional[str] = None
    until: Optional[str] = None

class FilterByLevelRequest(BaseModel):
    levels: Optional[List[str]] = None
    exclude_levels: Optional[List[str]] = None

class SummarizeRequest(BaseModel):
    time_range: Optional[Dict[str, str]] = None

class TrendingPatternsRequest(BaseModel):
    time_window: str = "hour"

class LogAIAgentConfig(BaseModel):
    provider: Literal["openai", "gemini", "claude", "ollama"] = "openai"
    api_key: Optional[str] = None
    model: Optional[str] = None
    host: Optional[str] = "http://localhost:11434"

# Dependency to check if agent is initialized
def get_initialized_agent():
    if logai_agent is None or not logai_agent.is_initialized:
        raise HTTPException(status_code=400, detail="LogAI Agent not initialized. Call /initialize first.")
    return logai_agent

# Routes
@app.post("/api/configure", response_model=dict)
async def configure_agent(config: LogAIAgentConfig):
    global logai_agent
    try:
        logai_agent = LogAIAgent(
            provider=config.provider,
            api_key=config.api_key,
            model=config.model,
            host=config.host
        )
        return {"success": True, "message": "Agent configured successfully"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/initialize", response_model=InitializeResponse)
async def initialize(request: InitializeRequest):
    global logai_agent
    
    if logai_agent is None:
        # Use default configuration if not explicitly configured
        logai_agent = LogAIAgent(provider="openai")
    
    try:
        success = logai_agent.initialize(request.log_file, request.format)
        if success:
            return InitializeResponse(success=True, message="LogAI Agent initialized successfully")
        else:
            return InitializeResponse(success=False, message="Failed to initialize LogAI Agent")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/upload-log-file")
async def upload_log_file(file: UploadFile = File(...), format: Optional[str] = Form(None)):
    global logai_agent
    
    if logai_agent is None:
        # Use default configuration if not explicitly configured
        logai_agent = LogAIAgent(provider="openai")
    
    try:
        # Save the uploaded file
        file_location = f"/tmp/{file.filename}"
        with open(file_location, "wb") as f:
            contents = await file.read()
            f.write(contents)
        
        # Initialize the agent with the uploaded file
        success = logai_agent.initialize(file_location, format)
        if success:
            return {"success": True, "message": "Log file uploaded and parsed successfully"}
        else:
            return {"success": False, "message": "Failed to parse the uploaded log file"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/search", response_model=List[Dict[str, Any]])
async def search_logs(request: SearchRequest, agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        results = agent.search_logs(request.query, request.limit)
        return results
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/execute-query", response_model=Dict[str, Any])
async def execute_query(request: ExecuteQueryRequest, agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        results = agent.execute_query(request.query)
        return results
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/template/{template_id}", response_model=Dict[str, Any])
async def get_template(template_id: int, agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        template = agent.get_template(template_id)
        if template is None:
            raise HTTPException(status_code=404, detail=f"Template with ID {template_id} not found")
        return template
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/time-range", response_model=Dict[str, str])
async def get_time_range(agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        time_range = agent.get_time_range()
        return time_range
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/count-occurrences", response_model=Dict[str, Any])
async def count_occurrences(request: CountOccurrencesRequest, agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        results = agent.count_occurrences(request.pattern, request.group_by)
        return results
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/summarize", response_model=Dict[str, Any])
async def summarize_logs(request: SummarizeRequest, agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        summary = agent.summarize_logs(request.time_range)
        return summary
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/filter-by-time", response_model=List[Dict[str, Any]])
async def filter_by_time(request: FilterByTimeRequest, agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        logs = agent.filter_by_time(request.since, request.until)
        return logs
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/filter-by-level", response_model=List[Dict[str, Any]])
async def filter_by_level(request: FilterByLevelRequest, agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        logs = agent.filter_by_level(request.levels, request.exclude_levels)
        return logs
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/statistics", response_model=Dict[str, Any])
async def calculate_statistics(agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        statistics = agent.calculate_statistics()
        return statistics
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/trending-patterns", response_model=List[Dict[str, Any]])
async def get_trending_patterns(request: TrendingPatternsRequest, agent: LogAIAgent = Depends(get_initialized_agent)):
    try:
        patterns = agent.get_trending_patterns(request.time_window)
        return patterns
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/health")
async def health_check():
    return {"status": "ok"}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("logai_server:app", host="0.0.0.0", port=8000, reload=True) 