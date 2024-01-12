<p>
  Provides Google TTS service for the Freeswitch (based on v1-rest api) <br>
</p>

### Usage example
```
<extension name="tts-test">
    <condition field="destination_number" expression="^(3333)$">
	<action application="answer"/>
	<action application="speak" data="google|en|Hello world!"/>
        <action application="sleep" data="1000"/>
        <action application="hangup"/>
    </condition>
</extension>

```
