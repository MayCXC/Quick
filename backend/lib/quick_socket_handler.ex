defmodule Quick.SocketHandler do
  @behaviour :cowboy_websocket

  require Logger

  def init(request, state) do
    {:cowboy_websocket, request, Map.merge(state, %{request_path: request.path})}
  end

  def websocket_init(state) do
    {:ok, _} = Registry.SocketHandler |> Registry.register(state.request_path, {})
    {:ok, state}
  end

  def websocket_handle({:text, message}, state) do
    Logger.info "Handle: #{message}"
    {:ok, state}
  end

  def websocket_info(message, state) do
    Logger.info "Info: #{message}"
    {:reply, {:text, message}, state}
  end
end
