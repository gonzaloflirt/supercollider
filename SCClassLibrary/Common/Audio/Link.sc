LinkTrig : UGen {

	*ar { arg quantum = 4, beats = 1;
		^this.multiNew('audio', quantum, beats)
	}
	*kr { arg quantum = 4, beats = 1;
		^this.multiNew('control', quantum, beats)
	}
	signalRange { ^\unipolar }
}

LinkPhase : UGen {

  *ar { arg quantum = 4;
    ^this.multiNew('audio', quantum)
  }
  *kr { arg quantum = 4, beats = 1;
    ^this.multiNew('control', quantum)
  }
  signalRange { ^\unipolar }
}
